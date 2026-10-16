#include "firebase_client.h"
#include "json_tree.h"
#include "alarm_manager.h"
#include "audio_player.h"
#include "sd_card.h"
#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CONFIG_M2_FIREBASE_SPARK
#define CONFIG_M2_FIREBASE_SPARK 0
#endif
#ifndef CONFIG_M2_FIREBASE_EMULATOR
#define CONFIG_M2_FIREBASE_EMULATOR 0
#endif

#if CONFIG_M2_FIREBASE_ENABLED
static const char *TAG = "firebase_client";
static char device_id[13], secret[65], pair_code[7], last_command[96];
static char last_ack[12] = "error";
static char id_token[2300];
static time_t token_until;
static bool started;
#if CONFIG_M2_FIREBASE_SPARK
static char device_uid[128];
static SemaphoreHandle_t cloud_mutex;
static unsigned pair_attempts;
static EventGroupHandle_t cloud_events;
#define DESIRED_CHANGED BIT0
#define COMMANDS_CHANGED BIT1
static esp_http_client_handle_t db_client;
static uint32_t rest_requests, stream_connections, stream_bytes;
static uint32_t desired_reads, command_reads, status_writes;
#endif

typedef struct { char *data; size_t capacity, used; bool failed; } json_writer_t;
static void append(json_writer_t *w, const char *format, ...)
{
    if (w->failed) return;
    va_list args;
    va_start(args,format);
    int n=vsnprintf(w->data+w->used,w->capacity-w->used,format,args);
    va_end(args);
    if (n<0 || (size_t)n>=w->capacity-w->used) { w->failed=true; return; }
    w->used+=n;
}
static void append_string(json_writer_t *w, const char *value)
{
    append(w,"\"");
    for (const unsigned char *p=(const unsigned char *)value; *p && !w->failed; ++p) {
        if (*p=='"' || *p=='\\') append(w,"\\%c",*p);
        else if (*p<32) append(w,"\\u%04x",*p);
        else append(w,"%c",*p);
    }
    append(w,"\"");
}

typedef struct { char *data; size_t used, capacity; } response_t;
static esp_err_t on_http(esp_http_client_event_t *event)
{
    response_t *r = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || !r) return ESP_OK;
    if (event->data_len < 0 || r->used + event->data_len >= r->capacity) return ESP_FAIL;
    memcpy(r->data + r->used, event->data, event->data_len);
    r->used += event->data_len;
    r->data[r->used] = 0;
    return ESP_OK;
}

static int request(const char *url, esp_http_client_method_t method, const char *body,
                   char *out, size_t capacity)
{
    response_t response = {.data = out, .capacity = capacity};
    if (capacity) out[0] = 0;
    esp_http_client_config_t config = {
        .url = url, .event_handler = on_http, .user_data = &response,
        .timeout_ms = 8000, .buffer_size = 1024, .buffer_size_tx = 3072,
#if !CONFIG_M2_FIREBASE_EMULATOR
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client;
#if CONFIG_M2_FIREBASE_SPARK
    bool reusable = !strncmp(url,CONFIG_M2_FIREBASE_DATABASE_BASE,strlen(CONFIG_M2_FIREBASE_DATABASE_BASE));
    if (reusable && db_client) {
        client=db_client;
        esp_http_client_set_url(client,url);
        esp_http_client_set_user_data(client,&response);
    } else {
        client=esp_http_client_init(&config);
        if (reusable) db_client=client;
    }
    ++rest_requests;
#else
    client = esp_http_client_init(&config);
#endif
    if (!client) return -1;
    esp_http_client_set_method(client, method);
    esp_http_client_set_post_field(client,body,body ? strlen(body) : 0);
    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
#if CONFIG_M2_FIREBASE_SPARK
    if (reusable && err==ESP_OK) esp_http_client_set_user_data(client,NULL);
    else {
        esp_http_client_cleanup(client);
        if (reusable) db_client=NULL;
    }
#else
    esp_http_client_cleanup(client);
#endif
    return status;
}

static bool field_string(const char *json, jt_token_t *t, int obj, const char *name,
                         char *out, size_t size)
{
    int index = jt_field(json, t, obj, name);
    return index >= 0 && jt_string(json, &t[index], out, size);
}

static bool field_int(const char *json, jt_token_t *t, int obj, const char *name, int64_t *out)
{
    int index = jt_field(json, t, obj, name);
    return index >= 0 && jt_int(json, &t[index], out);
}

static bool field_bool(const char *json, jt_token_t *t, int obj, const char *name, bool *out)
{
    int index = jt_field(json, t, obj, name);
    return index >= 0 && jt_bool(json, &t[index], out);
}

static void setup_identity(void)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
    snprintf(device_id, sizeof(device_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open("firebase_m2", NVS_READWRITE, &nvs));
    size_t size = sizeof(secret);
    if (nvs_get_str(nvs, "secret", secret, &size) != ESP_OK || size != sizeof(secret)) {
        const char hex[] = "0123456789abcdef";
        uint8_t random[32];
        esp_fill_random(random, sizeof(random));
        for (int i = 0; i < 32; ++i) {
            secret[i * 2] = hex[random[i] >> 4];
            secret[i * 2 + 1] = hex[random[i] & 15];
        }
        secret[64] = 0;
        ESP_ERROR_CHECK(nvs_set_str(nvs, "secret", secret));
    }
    size = sizeof(last_command);
    nvs_get_str(nvs, "last_cmd", last_command, &size);
    size=sizeof(last_ack);
    nvs_get_str(nvs,"last_ack",last_ack,&size);
    char scope[128] = {0}, previous[128] = {0};
    snprintf(scope,sizeof(scope),"%s:%s",CONFIG_M2_FIREBASE_PROJECT_ID,
             CONFIG_M2_FIREBASE_EMULATOR ? "emulator" : "cloud");
    size = sizeof(previous);
    nvs_get_str(nvs,"scope",previous,&size);
    if (strcmp(scope,previous)) {
        last_command[0] = 0;
        strcpy(last_ack,"error");
        ESP_ERROR_CHECK(nvs_set_str(nvs,"last_cmd",""));
        ESP_ERROR_CHECK(nvs_set_str(nvs,"scope",scope));
    }
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
    snprintf(pair_code, sizeof(pair_code), "%06" PRIu32, esp_random() % 1000000);
}
#endif

bool firebase_client_pairing(char *id, size_t id_size, char *code, size_t code_size)
{
#if CONFIG_M2_FIREBASE_ENABLED
    if (!started || id_size < sizeof(device_id) || code_size < sizeof(pair_code)) return false;
    strcpy(id, device_id);
    strcpy(code, pair_code);
    return true;
#else
    (void)id; (void)id_size; (void)code; (void)code_size;
    return false;
#endif
}

#if CONFIG_M2_FIREBASE_ENABLED
static bool db_request(const char *base, const char *path, esp_http_client_method_t method,
                       const char *body, char *out, size_t capacity);

static bool bases(char *functions, size_t fn_size, char *database, size_t db_size,
                  char *auth, size_t auth_size)
{
#if CONFIG_M2_FIREBASE_EMULATOR
    if (!CONFIG_M2_FIREBASE_HOST[0]) return false;
    snprintf(functions, fn_size, "http://%s:5001/%s/asia-southeast1",
             CONFIG_M2_FIREBASE_HOST, CONFIG_M2_FIREBASE_PROJECT_ID);
    snprintf(database, db_size, "http://%s:9000", CONFIG_M2_FIREBASE_HOST);
    snprintf(auth, auth_size, "http://%s:9099/identitytoolkit.googleapis.com",
             CONFIG_M2_FIREBASE_HOST);
#else
    if ((!CONFIG_M2_FIREBASE_SPARK && strncmp(CONFIG_M2_FIREBASE_FUNCTIONS_BASE, "https://", 8)) ||
        strncmp(CONFIG_M2_FIREBASE_DATABASE_BASE, "https://", 8)) return false;
    snprintf(functions, fn_size, "%s", CONFIG_M2_FIREBASE_FUNCTIONS_BASE);
    snprintf(database, db_size, "%s", CONFIG_M2_FIREBASE_DATABASE_BASE);
    snprintf(auth, auth_size, "https://identitytoolkit.googleapis.com");
#endif
    return true;
}

static bool enroll(const char *functions)
{
    char url[320], out[320];
    alarm_config_t *cfg=malloc(sizeof(*cfg));
    char *body=calloc(1,20000);
    if (!cfg || !body) { free(cfg); free(body); return false; }
    alarm_manager_get(cfg,NULL);
    json_writer_t w={.data=body,.capacity=20000};
    snprintf(url, sizeof(url), "%s/enrollDevice", functions);
    append(&w,"{\"deviceId\":\"%s\",\"secret\":\"%s\",\"code\":\"%s\","
              "\"initialDesired\":{\"revision\":1,\"timezone\":\"Asia/Ho_Chi_Minh\","
              "\"manualSound\":",device_id,secret,pair_code);
    append_string(&w,cfg->manual_sound);
    append(&w,",\"alarmCount\":%u,\"nextId\":%" PRIu32 ",\"alarms\":{",
           cfg->count,cfg->next_id);
    for (int i=0; i<cfg->count; ++i) {
        const alarm_item_t *a=&cfg->items[i];
        append(&w,"%s\"a_%" PRIu32 "\":{\"id\":%" PRIu32 ",\"enabled\":%s,"
                  "\"hour\":%u,\"minute\":%u,\"days\":%u,\"volume\":%u,"
                  "\"ringSeconds\":%u,\"snoozeMinutes\":%u,\"name\":",
               i?",":"",a->id,a->id,a->enabled?"true":"false",a->hour,a->minute,
               a->days,a->volume,a->ring_seconds,a->snooze_minutes);
        append_string(&w,a->name);
        append(&w,",\"sound\":");
        append_string(&w,a->sound);
        append(&w,"}");
    }
    append(&w,"}}}");
    int status;
#if CONFIG_M2_FIREBASE_SPARK
    jt_token_t *tokens=calloc(1200,sizeof(*tokens));
    int initial = tokens && !w.failed && jt_parse(body,strlen(body),tokens,1200)>0
        ? jt_field(body,tokens,0,"initialDesired") : -1;
    if (initial>=0) {
        body[tokens[initial].end]=0;
        snprintf(url,sizeof(url),"devices/%s/inbox/desired",device_id);
        status=db_request(functions,url,HTTP_METHOD_PUT,body+tokens[initial].start,out,sizeof(out)) ? 200 : -1;
    } else status=-1;
    free(tokens);
#else
    status=w.failed?-1:request(url,HTTP_METHOD_POST,body,out,sizeof(out));
#endif
    free(cfg); free(body);
    return status==200;
}

static bool login(const char *functions, const char *auth)
{
#if CONFIG_M2_FIREBASE_SPARK
    (void)functions;
    char url[360], body[320], email[100];
    char *out=calloc(1,6000);
    if (!out) return false;
    snprintf(email,sizeof(email),"esp32-%s-%.16s@device.invalid",device_id,secret);
    snprintf(body,sizeof(body),"{\"email\":\"%s\",\"password\":\"%s\",\"returnSecureToken\":true}",email,secret);
    snprintf(url,sizeof(url),"%s/v1/accounts:signInWithPassword?key=%s",auth,CONFIG_M2_FIREBASE_API_KEY);
    int status=request(url,HTTP_METHOD_POST,body,out,6000);
    if (status==400) {
        snprintf(url,sizeof(url),"%s/v1/accounts:signUp?key=%s",auth,CONFIG_M2_FIREBASE_API_KEY);
        status=request(url,HTTP_METHOD_POST,body,out,6000);
    }
    jt_token_t tokens[64];
    bool ok=status==200 && jt_parse(out,strlen(out),tokens,64)>0 &&
        field_string(out,tokens,0,"idToken",id_token,sizeof(id_token)) &&
        field_string(out,tokens,0,"localId",device_uid,sizeof(device_uid));
    if (ok) token_until=time(NULL)+3300;
    else ESP_LOGW(TAG,"Spark device sign-in failed: HTTP %d",status);
    free(out);
    return ok;
#else
    char url[360], body[160], out[3100];
    snprintf(url, sizeof(url), "%s/issueDeviceToken", functions);
    snprintf(body, sizeof(body), "{\"deviceId\":\"%s\",\"secret\":\"%s\"}", device_id, secret);
    int status = request(url, HTTP_METHOD_POST, body, out, sizeof(out));
    if (status != 200) {
        ESP_LOGW(TAG,"Device token request failed: HTTP %d",status);
        return false;
    }
    jt_token_t t[24];
    if (jt_parse(out, strlen(out), t, 24) < 0) return false;
    char custom[2300];
    if (!field_string(out, t, 0, "customToken", custom, sizeof(custom))) return false;
    snprintf(url, sizeof(url), "%s/v1/accounts:signInWithCustomToken?key=%s",
             auth, CONFIG_M2_FIREBASE_API_KEY);
    char *payload = malloc(strlen(custom) + 80);
    if (!payload) return false;
    sprintf(payload, "{\"token\":\"%s\",\"returnSecureToken\":true}", custom);
    status = request(url, HTTP_METHOD_POST, payload, out, sizeof(out));
    free(payload);
    if (status != 200) {
        ESP_LOGW(TAG,"Firebase Auth token exchange failed: HTTP %d",status);
        return false;
    }
    if (jt_parse(out, strlen(out), t, 24) < 0 ||
        !field_string(out, t, 0, "idToken", id_token, sizeof(id_token))) return false;
    token_until = time(NULL) + 3300;
    return true;
#endif
}

static bool db_request(const char *base, const char *path, esp_http_client_method_t method,
                       const char *body, char *out, size_t capacity)
{
    char url[2800];
    snprintf(url, sizeof(url), "%s/%s.json?auth=%s%s%s", base,
             path, id_token,
#if CONFIG_M2_FIREBASE_EMULATOR
             "&ns=", CONFIG_M2_FIREBASE_NAMESPACE
#else
             "", ""
#endif
             );
    if (method != HTTP_METHOD_GET) {
        size_t length=strlen(url);
        snprintf(url+length,sizeof(url)-length,"&print=silent");
    }
    int status=request(url, method, body, out, capacity);
    return status == 200 || status == 204;
}

#if CONFIG_M2_FIREBASE_SPARK
static bool spark_register(const char *database)
{
    char path[160], body[256], reply[1024];
    snprintf(path,sizeof(path),"registry/%s",device_id);
    if (!db_request(database,path,HTTP_METHOD_GET,NULL,reply,sizeof(reply))) return false;
    if (!strcmp(reply,"null")) {
        snprintf(body,sizeof(body),"{\"deviceUid\":\"%s\",\"revoked\":false}",device_uid);
        if (!db_request(database,path,HTTP_METHOD_PUT,body,reply,sizeof(reply))) return false;
    }
    snprintf(path,sizeof(path),"devices/%s/inbox/desired/revision",device_id);
    if (!db_request(database,path,HTTP_METHOD_GET,NULL,reply,sizeof(reply))) return false;
    return strcmp(reply,"null") || enroll(database);
}
#endif

static bool alarm_from_json(const char *json, jt_token_t *t, int obj, alarm_item_t *a)
{
    int64_t n;
    if (!field_int(json,t,obj,"id",&n) || n < 1 || n > UINT32_MAX) return false;
    a->id = n;
    if (!field_bool(json,t,obj,"enabled",&a->enabled)) return false;
#define NUM_FIELD(key, member, max) \
    do { \
        if (!field_int(json,t,obj,key,&n) || n < 0 || n > max) return false; \
        a->member = n; \
    } while (0)
    NUM_FIELD("hour", hour, 23);
    NUM_FIELD("minute", minute, 59);
    NUM_FIELD("days", days, 127);
    NUM_FIELD("volume", volume, 100);
    if (!field_int(json,t,obj,"ringSeconds",&n) || n < 5 || n > 30) return false;
    a->ring_seconds = n;
    NUM_FIELD("snoozeMinutes", snooze_minutes, 60);
#undef NUM_FIELD
    return field_string(json,t,obj,"name",a->name,sizeof(a->name)) &&
           field_string(json,t,obj,"sound",a->sound,sizeof(a->sound));
}

static bool sync_desired(const char *base)
{
    bool ok=false;
#if CONFIG_M2_FIREBASE_SPARK
    ++desired_reads;
#endif
    char path[128];
    snprintf(path,sizeof(path),"devices/%s/inbox/desired",device_id);
    char *out = calloc(1, 13000);
    jt_token_t *t = calloc(1200, sizeof(*t));
    if (!out || !t) goto done;
    if (!db_request(base,path,HTTP_METHOD_GET,NULL,out,13000)) goto done;
    ok=true;
    if (jt_parse(out,strlen(out),t,1200) < 0 || t[0].type != JT_OBJECT) goto done;
    int64_t revision, count, next_id;
    char zone[40];
    if (!field_int(out,t,0,"revision",&revision) || revision < 1 || revision > UINT32_MAX ||
        revision <= alarm_manager_remote_revision() ||
        !field_int(out,t,0,"alarmCount",&count) || count < 0 || count > ALARM_MAX ||
        !field_int(out,t,0,"nextId",&next_id) || next_id < 1 || next_id > UINT32_MAX ||
        !field_string(out,t,0,"timezone",zone,sizeof(zone)) || strcmp(zone,"Asia/Ho_Chi_Minh")) goto done;
    alarm_config_t *cfg = calloc(1,sizeof(*cfg));
    if (!cfg) goto done;
    cfg->revision = revision;
    cfg->next_id = next_id;
    if (!field_string(out,t,0,"manualSound",cfg->manual_sound,sizeof(cfg->manual_sound))) goto free_cfg;
    int alarms = jt_field(out,t,0,"alarms");
    if (count && (alarms < 0 || t[alarms].type != JT_OBJECT || t[alarms].count != count)) goto free_cfg;
    if (alarms >= 0 && t[alarms].type == JT_OBJECT) {
        int child = alarms + 1;
        for (int i=0; i<t[alarms].count; ++i) {
            int value = t[child].next;
            if (cfg->count >= ALARM_MAX || !alarm_from_json(out,t,value,&cfg->items[cfg->count])) goto free_cfg;
            ++cfg->count;
            child = t[value].next;
        }
    }
    if (cfg->count == count && alarm_manager_apply_remote(cfg,revision) == ESP_OK)
        ESP_LOGI(TAG,"Applied remote revision %" PRIi64,revision);
free_cfg:
    free(cfg);
done:
    free(t); free(out);
    return ok;
}

static bool remember_command(const char *key)
{
    nvs_handle_t nvs;
    if (nvs_open("firebase_m2", NVS_READWRITE, &nvs) != ESP_OK) return false;
    bool stored = nvs_set_str(nvs,"last_cmd",key) == ESP_OK &&
        nvs_set_str(nvs,"last_ack","error") == ESP_OK && nvs_commit(nvs) == ESP_OK;
    if (stored)
        { snprintf(last_command,sizeof(last_command),"%s",key); strcpy(last_ack,"error"); }
    nvs_close(nvs);
    return stored;
}

static void remember_ack(const char *state)
{
    nvs_handle_t nvs;
    if (nvs_open("firebase_m2",NVS_READWRITE,&nvs)!=ESP_OK) return;
    if (nvs_set_str(nvs,"last_ack",state)==ESP_OK && nvs_commit(nvs)==ESP_OK)
        snprintf(last_ack,sizeof(last_ack),"%s",state);
    nvs_close(nvs);
}

static bool send_ack(const char *base,const char *key,const char *state)
{
    char path[190],body[100],reply[120];
    snprintf(path,sizeof(path),"devices/%s/acks/%s",device_id,key);
    snprintf(body,sizeof(body),"{\"state\":\"%s\",\"at\":%" PRIi64 "}",state,(int64_t)time(NULL)*1000);
    bool ok=db_request(base,path,HTTP_METHOD_PUT,body,reply,sizeof(reply));
#if CONFIG_M2_FIREBASE_SPARK
    if (ok) {
        snprintf(path,sizeof(path),"devices/%s/inbox/commands/%s",device_id,key);
        ok=db_request(base,path,HTTP_METHOD_DELETE,NULL,reply,sizeof(reply));
    }
#endif
    return ok;
}

static bool sync_commands(const char *base)
{
    bool ok=false;
#if CONFIG_M2_FIREBASE_SPARK
    ++command_reads;
#endif
    char path[160];
    char *out = calloc(1, 6000);
    jt_token_t *t = calloc(256, sizeof(*t));
    if (!out || !t) goto done;
    snprintf(path,sizeof(path),"devices/%s/inbox/commands",device_id);
    if (!db_request(base,path,HTTP_METHOD_GET,NULL,out,6000)) goto done;
    ok=true;
    if (jt_parse(out,strlen(out),t,256) < 0 || t[0].type != JT_OBJECT) goto done;
    /* REST JSON object order is not guaranteed. Process keys in sorted order. */
    int child=1;
    for (int i=0;i<t[0].count;++i) {
        int value=t[child].next;
        char key[96];
        if (jt_string(out,&t[child],key,sizeof(key)) && !strcmp(key,last_command))
            if (!send_ack(base,key,last_ack)) ok=false;
        child=t[value].next;
    }
    for (int i=0; i<t[0].count; ++i) {
        int value=-1;
        char key[96], type[24]; int64_t expires;
        key[0]=0;
        child=1;
        for (int j=0;j<t[0].count;++j) {
            int candidate=t[child].next;
            char next_key[96];
            if (jt_string(out,&t[child],next_key,sizeof(next_key)) && strcmp(next_key,last_command)>0 &&
                (!key[0] || strcmp(next_key,key)<0)) {
                strcpy(key,next_key); value=candidate;
            }
            child=t[candidate].next;
        }
        if (value<0) break;
        if (
            field_string(out,t,value,"type",type,sizeof(type)) &&
            field_int(out,t,value,"expiresAt",&expires)) {
            bool expired = time(NULL)*1000 > expires;
            /* Persist before the side effect: a reset cannot replay a command. */
            if (!remember_command(key)) { ok=false; break; }
            if (!expired) {
                if (!strcmp(type,"playNow")) alarm_manager_play_now();
                else if (!strcmp(type,"stop")) alarm_manager_stop();
                else if (!strcmp(type,"snooze")) alarm_manager_snooze();
            }
            remember_ack(expired ? "expired" : "done");
            if (!send_ack(base,key,last_ack)) { ok=false; break; }
        } else {
            /* A malformed entry cannot block the later valid commands. */
            if (!remember_command(key) || !send_ack(base,key,"error")) { ok=false; break; }
        }
    }
done:
    free(t); free(out);
    return ok;
}

static void report_status(const char *base)
{
    alarm_state_t state;
    alarm_manager_get(NULL,&state);
    bool playing = false;
    char current[4];
    audio_player_get_status(&playing,current,sizeof(current));
    bool mounted = sd_card_lock();
    if (mounted) sd_card_unlock();
#if CONFIG_M2_FIREBASE_SPARK
    static char previous[100];
    static int64_t last_sent, retry_at;
    int64_t now=esp_timer_get_time();
    char signature[100];
    snprintf(signature,sizeof(signature),"%lu:%d:%d:%d",
             (unsigned long)alarm_manager_remote_revision(),state.clock_valid,mounted,playing);
    if (now<retry_at || (last_sent && !strcmp(signature,previous) && now-last_sent<60000000)) return;
#endif
    char path[150], body[240], reply[100];
    snprintf(path,sizeof(path),"devices/%s/reported",device_id);
    snprintf(body,sizeof(body),"{\"appliedRevision\":%" PRIu32 ",\"lastSeen\":%" PRIi64
             ",\"clockSynced\":%s,\"sdMounted\":%s,\"playing\":%s}",
             alarm_manager_remote_revision(),(int64_t)time(NULL)*1000,
             state.clock_valid?"true":"false",mounted?"true":"false",
             playing?"true":"false");
    bool sent=db_request(base,path,HTTP_METHOD_PUT,body,reply,sizeof(reply));
#if CONFIG_M2_FIREBASE_SPARK
    if (sent) {
        ++status_writes;
        strcpy(previous,signature);
        last_sent=esp_timer_get_time();
        retry_at=0;
    } else retry_at=esp_timer_get_time()+5000000;
#else
    (void)sent;
#endif
}

#if CONFIG_M2_FIREBASE_SPARK
/* Firebase sends one JSON data line per SSE frame. TCP reads may split anywhere. */
static bool stream_event(char *frame, jt_token_t *tokens)
{
    char *event=NULL, *data=NULL, *save=NULL;
    for (char *line=strtok_r(frame,"\n",&save); line; line=strtok_r(NULL,"\n",&save)) {
        if (!strncmp(line,"event:",6)) { event=line+6; while (*event==' ') ++event; }
        else if (!strncmp(line,"data:",5)) { data=line+5; while (*data==' ') ++data; }
    }
    if (!event || !strcmp(event,"keep-alive")) return true;
    if (!strcmp(event,"cancel") || !strcmp(event,"auth_revoked")) return false;
    if ((strcmp(event,"put") && strcmp(event,"patch")) || !data) return true;
    char path[200];
    if (jt_parse(data,strlen(data),tokens,1200)<0 ||
        !field_string(data,tokens,0,"path",path,sizeof(path))) return false;
    int value=jt_field(data,tokens,0,"data");
    if (value<0) return false;
    EventBits_t changes=0;
    if (!strcmp(path,"/")) changes=DESIRED_CHANGED|COMMANDS_CHANGED;
    else if (!strcmp(path,"/desired") || !strncmp(path,"/desired/",9)) changes=DESIRED_CHANGED;
    else if ((!strcmp(path,"/commands") || !strncmp(path,"/commands/",10)) &&
             tokens[value].type!=JT_NULL) changes=COMMANDS_CHANGED;
    // Deleting an acknowledged command does not trigger another queue read.
    if (changes) xEventGroupSetBits(cloud_events,changes);
    return true;
}

static void stream_task(void *unused)
{
    (void)unused;
    char *url=malloc(3000), *frame=malloc(16384);
    jt_token_t *tokens=calloc(1200,sizeof(*tokens));
    if (!url || !frame || !tokens) {
        free(url); free(frame); free(tokens);
        ESP_LOGE(TAG,"Cannot allocate realtime stream buffers");
        vTaskDelete(NULL);
        return;
    }
    unsigned backoff=2;
    for (;;) {
        time_t expires;
        xSemaphoreTake(cloud_mutex,portMAX_DELAY);
        expires=token_until;
        snprintf(url,3000,"%s/devices/%s/inbox.json?auth=%s",
                 CONFIG_M2_FIREBASE_DATABASE_BASE,device_id,id_token);
        xSemaphoreGive(cloud_mutex);
        if (time(NULL)>=expires) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        esp_http_client_config_t config={.url=url,.timeout_ms=8000,
            .buffer_size=1024,.buffer_size_tx=3072,.crt_bundle_attach=esp_crt_bundle_attach,
            .disable_auto_redirect=true};
        esp_http_client_handle_t client=esp_http_client_init(&config);
        bool opened=false;
        if (client) {
            esp_http_client_set_header(client,"Accept","text/event-stream");
            for (int redirect=0; redirect<5; ++redirect) {
                if (esp_http_client_open(client,0)!=ESP_OK || esp_http_client_fetch_headers(client)<0) break;
                int status=esp_http_client_get_status_code(client);
                if (status==200) { opened=true; break; }
                if (status!=307 && status!=301 && status!=302) break;
                if (esp_http_client_set_redirection(client)!=ESP_OK) break;
                esp_http_client_close(client);
            }
        }
        if (opened) {
            ++stream_connections;
            ESP_LOGI(TAG,"Realtime inbox connected");
            esp_http_client_set_timeout_ms(client,1000);
            size_t used=0;
            int64_t last_data=esp_timer_get_time();
            bool healthy=true;
            while (healthy && time(NULL)<expires) {
                char chunk[1024];
                int count=esp_http_client_read(client,chunk,sizeof(chunk));
                if (count==-ESP_ERR_HTTP_EAGAIN) {
                    if (esp_timer_get_time()-last_data>90000000) break;
                    continue;
                }
                if (count<=0) break;
                last_data=esp_timer_get_time();
                backoff=2;
                stream_bytes+=count;
                for (int i=0; i<count && healthy; ++i) {
                    if (chunk[i]=='\r') continue;
                    if (used>=16383) { healthy=false; break; }
                    frame[used++]=chunk[i];
                    if (used>=2 && frame[used-1]=='\n' && frame[used-2]=='\n') {
                        frame[used]=0;
                        healthy=stream_event(frame,tokens);
                        used=0;
                    }
                }
            }
        }
        if (client) esp_http_client_cleanup(client);
        ESP_LOGW(TAG,"Realtime inbox disconnected; retry in %u s",backoff);
        vTaskDelay(pdMS_TO_TICKS(backoff*1000));
        if (backoff<60) backoff=backoff*2>60 ? 60 : backoff*2;
    }
}
#endif

static void task(void *unused)
{
    (void)unused;
    char functions[300], database[300], auth[200];
    if (!bases(functions,sizeof(functions),database,sizeof(database),auth,sizeof(auth))) {
        ESP_LOGE(TAG,"Firebase host/base URL is missing or invalid");
        vTaskDelete(NULL);
    }
#if CONFIG_M2_FIREBASE_SPARK
    bool registered=false, streaming=false;
#else
    time_t last_enroll = 0;
#endif
    for (;;) {
#if CONFIG_M2_FIREBASE_SPARK
        EventBits_t changes=xEventGroupWaitBits(cloud_events,DESIRED_CHANGED|COMMANDS_CHANGED,
                                               pdTRUE,pdFALSE,pdMS_TO_TICKS(250));
#endif
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
        time_t now = time(NULL);
#if CONFIG_M2_FIREBASE_SPARK
        if (now < 1700000000) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
        xSemaphoreTake(cloud_mutex,portMAX_DELAY);
        if (now >= token_until && !login(functions,auth)) {
            xSemaphoreGive(cloud_mutex);
            vTaskDelay(pdMS_TO_TICKS(30000)); continue;
        }
        if (!registered) {
            if (!spark_register(database)) {
                ESP_LOGW(TAG,"Spark registration/configuration not ready");
                xSemaphoreGive(cloud_mutex);
                vTaskDelay(pdMS_TO_TICKS(10000)); continue;
            }
            registered=true;
        }
        if (!streaming) {
            streaming=xTaskCreate(stream_task,"firebase_sse",16384,NULL,4,NULL)==pdPASS;
            changes|=DESIRED_CHANGED|COMMANDS_CHANGED;
        }
        bool synced=true;
        if (changes & COMMANDS_CHANGED) synced=sync_commands(database) && synced;
        if (changes & DESIRED_CHANGED) synced=sync_desired(database) && synced;
        report_status(database);
        xSemaphoreGive(cloud_mutex);
        if (!synced) {
            xEventGroupSetBits(cloud_events,changes);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
#else
        if (now-last_enroll >= 120 || !last_enroll) {
            if (enroll(functions)) last_enroll=now;
        }
        if (now >= token_until && !login(functions,auth)) {
            vTaskDelay(pdMS_TO_TICKS(2000)); continue;
        }
        sync_desired(database);
        sync_commands(database);
        report_status(database);
        vTaskDelay(pdMS_TO_TICKS(2000));
#endif
    }
}
#endif

void firebase_client_start(void)
{
#if CONFIG_M2_FIREBASE_ENABLED
    setup_identity();
#if CONFIG_M2_FIREBASE_SPARK
    cloud_mutex=xSemaphoreCreateMutex();
    configASSERT(cloud_mutex);
    cloud_events=xEventGroupCreate();
    configASSERT(cloud_events);
#endif
    started = true;
    ESP_LOGI(TAG,"Pair device %s, code %s",device_id,pair_code);
    // Real TLS certificate verification needs more stack than emulator HTTP.
    xTaskCreate(task,"firebase_m2",20480,NULL,4,NULL);
#endif
}

int firebase_client_pair_request(const char *json)
{
#if CONFIG_M2_FIREBASE_ENABLED && CONFIG_M2_FIREBASE_SPARK
    if (!started || !cloud_mutex || !json) return 503;
    if (xSemaphoreTake(cloud_mutex,pdMS_TO_TICKS(15000)) != pdTRUE) return 503;
    int result=503;
    char *out=calloc(1,6500), *payload=calloc(1,2600);
    jt_token_t *t=calloc(160,sizeof(*t));
    char code[7], id[13], token[2300], uid[128], path[160], url[400];
    if (!out || !payload || !t || time(NULL)>=token_until) goto done_pair;
    result=400;
    if (jt_parse(json,strlen(json),t,160)<0 ||
        !field_string(json,t,0,"deviceId",id,sizeof(id)) || strcmp(id,device_id) ||
        !field_string(json,t,0,"code",code,sizeof(code)) ||
        !field_string(json,t,0,"idToken",token,sizeof(token))) goto done_pair;
    result=403;
    if (pair_attempts>=5) goto done_pair;
    if (strcmp(code,pair_code)) { ++pair_attempts; goto done_pair; }
    snprintf(url,sizeof(url),"https://identitytoolkit.googleapis.com/v1/accounts:lookup?key=%s",CONFIG_M2_FIREBASE_API_KEY);
    snprintf(payload,2600,"{\"idToken\":\"%s\"}",token);
    result=401;
    if (request(url,HTTP_METHOD_POST,payload,out,6500)!=200 || jt_parse(out,strlen(out),t,160)<0) goto done_pair;
    int users=jt_field(out,t,0,"users"), user=users>=0 ? jt_array_item(t,users,0) : -1;
    if (user<0 || !field_string(out,t,user,"localId",uid,sizeof(uid)) || !strcmp(uid,device_uid)) goto done_pair;
    snprintf(path,sizeof(path),"registry/%s/ownerUid",device_id);
    result=503;
    if (!db_request(CONFIG_M2_FIREBASE_DATABASE_BASE,path,HTTP_METHOD_GET,NULL,out,6500)) goto done_pair;
    if (strcmp(out,"null")) {
        if (jt_parse(out,strlen(out),t,160)<0 || !jt_string(out,&t[0],token,sizeof(token))) goto done_pair;
        result=strcmp(token,uid) ? 409 : 200;
        goto done_pair;
    }
    snprintf(payload,2600,"\"%s\"",uid);
    if (db_request(CONFIG_M2_FIREBASE_DATABASE_BASE,path,HTTP_METHOD_PUT,payload,out,6500)) {
        pair_attempts=0;
        result=200;
        ESP_LOGI(TAG,"Spark owner paired successfully");
    }
done_pair:
    free(out); free(payload); free(t);
    xSemaphoreGive(cloud_mutex);
    return result;
#else
    (void)json;
    return 503;
#endif
}

void firebase_client_metrics(char *out, size_t capacity)
{
#if CONFIG_M2_FIREBASE_ENABLED && CONFIG_M2_FIREBASE_SPARK
    snprintf(out,capacity,"{\"restRequests\":%lu,\"streamConnections\":%lu,\"streamBytes\":%lu,"
             "\"desiredReads\":%lu,\"commandReads\":%lu,\"statusWrites\":%lu}",
             (unsigned long)rest_requests,(unsigned long)stream_connections,(unsigned long)stream_bytes,
             (unsigned long)desired_reads,(unsigned long)command_reads,(unsigned long)status_writes);
#else
    snprintf(out,capacity,"{}");
#endif
}
