#include "alarm_web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alarm_manager.h"

static bool escape_json(const char *src, char *dst, size_t size)
{
    size_t j = 0;
    for (size_t i = 0; src[i]; ++i) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '"' || ch == '\\') {
            if (j + 2 >= size) return false;
            dst[j++] = '\\'; dst[j++] = ch;
        } else if (ch < 32) {
            if (j + 6 >= size) return false;
            j += snprintf(dst + j, size - j, "\\u%04x", ch);
        } else {
            if (j + 1 >= size) return false;
            dst[j++] = ch;
        }
    }
    dst[j] = 0;
    return true;
}

static esp_err_t get_alarms(httpd_req_t *req)
{
    alarm_config_t config;
    alarm_state_t state;
    alarm_manager_get(&config, &state);
    char buf[768];
    char manual[ALARM_SOUND_MAX * 2 + 1];
    if (!escape_json(config.manual_sound, manual, sizeof(manual)))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Bad sound name");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    int len = snprintf(buf, sizeof(buf),
        "{\"revision\":%lu,\"manualSound\":\"%s\",\"clockValid\":%s,"
        "\"now\":%lld,\"nextAt\":%lld,\"nextId\":%lu,\"activeId\":%lu,"
        "\"ringing\":%s,\"manual\":%s,\"ringUntil\":%lld,\"alarms\":[",
        (unsigned long)config.revision, manual, state.clock_valid ? "true" : "false",
        (long long)state.now, (long long)state.next_at,
        (unsigned long)state.next_id, (unsigned long)state.active_id,
        state.ringing ? "true" : "false", state.manual ? "true" : "false",
        (long long)state.ring_until);
    if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) return ESP_FAIL;
    for (int i = 0; i < config.count; ++i) {
        const alarm_item_t *item = &config.items[i];
        char name[ALARM_NAME_MAX * 6 + 1], sound[ALARM_SOUND_MAX * 6 + 1];
        if (!escape_json(item->name, name, sizeof(name)) ||
            !escape_json(item->sound, sound, sizeof(sound))) return ESP_FAIL;
        len = snprintf(buf, sizeof(buf),
            "%s{\"id\":%lu,\"enabled\":%s,\"hour\":%u,\"minute\":%u,"
            "\"days\":%u,\"volume\":%u,\"ringSeconds\":%u,\"snoozeMinutes\":%u,"
            "\"name\":\"%s\",\"sound\":\"%s\"}",
            i ? "," : "", (unsigned long)item->id, item->enabled ? "true" : "false",
            item->hour, item->minute, item->days, item->volume,
            item->ring_seconds, item->snooze_minutes, name, sound);
        if (len < 0 || len >= sizeof(buf) || httpd_resp_send_chunk(req, buf, len) != ESP_OK) return ESP_FAIL;
    }
    httpd_resp_send_chunk(req, "]}", 2);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static bool decode(char *text)
{
    size_t out = 0;
    for (size_t i = 0; text[i]; ++i) {
        unsigned char ch = text[i];
        if (ch == '+') ch = ' ';
        else if (ch == '%') {
            if (!text[i + 1] || !text[i + 2]) return false;
            char digits[3] = {text[i + 1], text[i + 2], 0};
            i += 2;
            char *end;
            long code = strtol(digits, &end, 16);
            if (*end || code == 0) return false;
            ch = (unsigned char)code;
        }
        if (ch < 32 && ch != '\t') return false;
        text[out++] = ch;
    }
    text[out] = 0;
    return true;
}

static bool field(const char *body, const char *key, char *value, size_t size)
{
    if (httpd_query_key_value(body, key, value, size) != ESP_OK) return false;
    return decode(value);
}

static bool number(const char *body, const char *key, unsigned maximum, unsigned *value)
{
    char data[24];
    if (!field(body, key, data, sizeof(data)) || !data[0]) return false;
    for (size_t i = 0; data[i]; ++i) if (data[i] < '0' || data[i] > '9') return false;
    char *end;
    unsigned long n = strtoul(data, &end, 10);
    if (*end || n > maximum) return false;
    *value = n;
    return true;
}

static bool read_form(httpd_req_t *req, char *body, size_t capacity)
{
    if (req->content_len <= 0 || req->content_len >= capacity) return false;
    int total = 0;
    while (total < req->content_len) {
        int n = httpd_req_recv(req, body + total, req->content_len - total);
        if (n <= 0) return false;
        total += n;
    }
    body[total] = 0;
    return true;
}

static esp_err_t save_alarm(httpd_req_t *req)
{
    char body[1024];
    alarm_item_t item = {0};
    unsigned n;
    if (!read_form(req, body, sizeof(body)) ||
        !number(body, "id", UINT32_MAX, &n)) goto invalid;
    item.id = n;
    if (!field(body, "name", item.name, sizeof(item.name)) ||
        !field(body, "sound", item.sound, sizeof(item.sound)) ||
        !number(body, "enabled", 1, &n)) goto invalid;
    item.enabled = n;
    if (!number(body, "hour", 23, &n)) goto invalid;
    item.hour = n;
    if (!number(body, "minute", 59, &n)) goto invalid;
    item.minute = n;
    if (!number(body, "days", 127, &n)) goto invalid;
    item.days = n;
    if (!number(body, "volume", 100, &n)) goto invalid;
    item.volume = n;
    if (!number(body, "ringSeconds", 30, &n)) goto invalid;
    item.ring_seconds = n;
    if (!number(body, "snoozeMinutes", 60, &n)) goto invalid;
    item.snooze_minutes = n;
    uint32_t id;
    esp_err_t err = alarm_manager_put(&item, &id);
    if (err == ESP_ERR_NOT_FOUND) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Alarm not found");
    if (err == ESP_ERR_NO_MEM) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Maximum 20 alarms");
    }
    if (err == ESP_ERR_INVALID_ARG) goto invalid;
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save alarm");
    char response[40];
    snprintf(response, sizeof(response), "{\"id\":%lu}", (unsigned long)id);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
invalid:
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid alarm fields");
}

static esp_err_t delete_alarm(httpd_req_t *req)
{
    char body[128];
    unsigned id;
    if (!read_form(req, body, sizeof(body)) || !number(body, "id", UINT32_MAX, &id) || id == 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid id");
    esp_err_t err = alarm_manager_delete(id);
    if (err == ESP_ERR_NOT_FOUND) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Alarm not found");
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete alarm");
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t manual_sound(httpd_req_t *req)
{
    char body[256], sound[ALARM_SOUND_MAX];
    if (!read_form(req, body, sizeof(body)) || !field(body, "sound", sound, sizeof(sound)) ||
        alarm_manager_set_manual_sound(sound) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid WAV filename");
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t play_now(httpd_req_t *req)
{
    alarm_manager_play_now();
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t snooze(httpd_req_t *req)
{
    alarm_manager_snooze();
    return httpd_resp_sendstr(req, "OK");
}

void alarm_web_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri="/api/alarms", .method=HTTP_GET, .handler=get_alarms},
        {.uri="/api/alarms/save", .method=HTTP_POST, .handler=save_alarm},
        {.uri="/api/alarms/delete", .method=HTTP_POST, .handler=delete_alarm},
        {.uri="/api/manual-sound", .method=HTTP_POST, .handler=manual_sound},
        {.uri="/api/play-now", .method=HTTP_POST, .handler=play_now},
        {.uri="/api/snooze", .method=HTTP_POST, .handler=snooze},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
        httpd_register_uri_handler(server, &routes[i]);
}
