#include "alarm_manager.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "audio_player.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sd_card.h"

#define STORE_VERSION 2U
#define CLOCK_VALID_AFTER 1704067200 /* 2024-01-01 UTC */
#define DEFAULT_RING_SECONDS 60
static const char *TAG = "alarm_manager";

typedef struct {
    uint32_t version;
    alarm_config_t config;
    int64_t fired_minute[ALARM_MAX];
    uint32_t applied_remote_revision;
    uint32_t crc;
} saved_t;

typedef struct {
    uint32_t version;
    alarm_config_t config;
    int64_t fired_minute[ALARM_MAX];
    uint32_t crc;
} saved_v1_t;

static saved_t saved;
static alarm_state_t state;
static SemaphoreHandle_t mutex;
static int64_t snooze_at;
static alarm_item_t snooze_item;
static bool manual_fallback;

static uint32_t crc32(const void *data, size_t size)
{
    uint32_t crc = 0xffffffffU;
    const uint8_t *p = data;
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1U));
    }
    return ~crc;
}

static esp_err_t save_locked(void)
{
    saved.crc = crc32(&saved, offsetof(saved_t, crc));
    nvs_handle_t handle;
    esp_err_t err = nvs_open("alarm", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "config", &saved, sizeof(saved));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(err));
    return err;
}

static bool sound_safe(const char *name)
{
    size_t n = strnlen(name, ALARM_SOUND_MAX);
    if (n >= ALARM_SOUND_MAX) return false;
    if (n == 0) return true;
    if (n < 5 || strcasecmp(name + n - 4, ".wav") != 0 ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 32 || ch == 127 || ch == '/' || ch == '\\' || ch == ':') return false;
    }
    return true;
}

static bool sound_exists(const char *name)
{
    if (name[0] == '\0') return false;
    if (!sd_card_lock()) return false;
    char path[128];
    snprintf(path, sizeof(path), "/sdcard/%s", name);
    struct stat st;
    bool found = stat(path, &st) == 0 && (st.st_mode & S_IFREG);
    sd_card_unlock();
    return found;
}

static void start_sound_locked(const char *name)
{
    if (sound_exists(name)) {
        char path[128];
        snprintf(path, sizeof(path), "/sdcard/%s", name);
        audio_player_play_loop(path);
    } else {
        if (name[0]) ESP_LOGW(TAG, "Sound unavailable, using embedded audio: %s", name);
        audio_player_play_embedded();
    }
}

static int find_index(uint32_t id)
{
    for (int i = 0; i < saved.config.count; ++i)
        if (saved.config.items[i].id == id) return i;
    return -1;
}

static bool valid_item(const alarm_item_t *item)
{
    return item && item->hour < 24 && item->minute < 60 &&
        (item->days & 0x80) == 0 && item->volume <= 100 &&
        item->ring_seconds >= 5 && item->ring_seconds <= 30 &&
        item->snooze_minutes <= 60 &&
        memchr(item->name, 0, sizeof(item->name)) != NULL && sound_safe(item->sound);
}

static time_t next_occurrence(const alarm_item_t *item, time_t now)
{
    if (!item->enabled) return 0;
    struct tm local;
    localtime_r(&now, &local);
    for (int offset = 0; offset <= 7; ++offset) {
        struct tm candidate = local;
        candidate.tm_mday += offset;
        candidate.tm_hour = item->hour;
        candidate.tm_min = item->minute;
        candidate.tm_sec = 0;
        candidate.tm_isdst = -1;
        time_t at = mktime(&candidate);
        if (at <= now || (item->days && !(item->days & (1U << candidate.tm_wday)))) continue;
        return at;
    }
    return 0;
}

static void refresh_next_locked(time_t now)
{
    state.next_at = 0;
    state.next_id = 0;
    if (!state.clock_valid) return;
    for (int i = 0; i < saved.config.count; ++i) {
        time_t at = next_occurrence(&saved.config.items[i], now);
        if (at && (!state.next_at || at < state.next_at ||
                   (at == state.next_at && saved.config.items[i].id < state.next_id))) {
            state.next_at = at;
            state.next_id = saved.config.items[i].id;
        }
    }
    if (snooze_at > now && (!state.next_at || snooze_at < state.next_at)) {
        state.next_at = snooze_at;
        state.next_id = snooze_item.id;
    }
}

void alarm_manager_get(alarm_config_t *config, alarm_state_t *out)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (config) *config = saved.config;
    if (out) *out = state;
    xSemaphoreGive(mutex);
}

esp_err_t alarm_manager_put(const alarm_item_t *item, uint32_t *id)
{
    if (!valid_item(item)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    int index = item->id ? find_index(item->id) : -1;
    if (item->id && index < 0) { xSemaphoreGive(mutex); return ESP_ERR_NOT_FOUND; }
    if (index < 0 && saved.config.count >= ALARM_MAX) {
        xSemaphoreGive(mutex); return ESP_ERR_NO_MEM;
    }
    alarm_item_t copy = *item;
    if (index < 0) {
        index = saved.config.count++;
        copy.id = saved.config.next_id++;
        saved.fired_minute[index] = -1;
    }
    saved.config.items[index] = copy;
    if (snooze_at && snooze_item.id == copy.id && !copy.enabled) snooze_at = 0;
    ++saved.config.revision;
    esp_err_t err = save_locked();
    if (id) *id = copy.id;
    xSemaphoreGive(mutex);
    return err;
}

esp_err_t alarm_manager_delete(uint32_t id)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    int index = find_index(id);
    if (index < 0) { xSemaphoreGive(mutex); return ESP_ERR_NOT_FOUND; }
    if (snooze_at && snooze_item.id == id) snooze_at = 0;
    for (int i = index; i + 1 < saved.config.count; ++i) {
        saved.config.items[i] = saved.config.items[i + 1];
        saved.fired_minute[i] = saved.fired_minute[i + 1];
    }
    --saved.config.count;
    ++saved.config.revision;
    esp_err_t err = save_locked();
    xSemaphoreGive(mutex);
    return err;
}

esp_err_t alarm_manager_set_manual_sound(const char *sound)
{
    if (!sound || !sound_safe(sound)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    snprintf(saved.config.manual_sound, sizeof(saved.config.manual_sound), "%s", sound);
    ++saved.config.revision;
    esp_err_t err = save_locked();
    xSemaphoreGive(mutex);
    return err;
}

uint32_t alarm_manager_remote_revision(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    uint32_t revision = saved.applied_remote_revision;
    xSemaphoreGive(mutex);
    return revision;
}

esp_err_t alarm_manager_apply_remote(const alarm_config_t *config, uint32_t remote_revision)
{
    if (!config || !remote_revision || config->count > ALARM_MAX ||
        !sound_safe(config->manual_sound))
        return ESP_ERR_INVALID_ARG;
    uint32_t max_id = 0;
    for (int i = 0; i < config->count; ++i) {
        if (!valid_item(&config->items[i]) || config->items[i].id == 0)
            return ESP_ERR_INVALID_ARG;
        for (int j = 0; j < i; ++j)
            if (config->items[j].id == config->items[i].id) return ESP_ERR_INVALID_ARG;
        if (config->items[i].id > max_id) max_id = config->items[i].id;
    }
    if (max_id == UINT32_MAX) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (remote_revision <= saved.applied_remote_revision) {
        xSemaphoreGive(mutex);
        return ESP_ERR_INVALID_STATE;
    }
    int64_t fired[ALARM_MAX];
    for (int i = 0; i < config->count; ++i) {
        int old = find_index(config->items[i].id);
        fired[i] = old >= 0 ? saved.fired_minute[old] : -1;
    }
    saved_t previous = saved;
    uint32_t next_id = saved.config.next_id;
    uint32_t revision = saved.config.revision + 1;
    saved.config = *config;
    saved.config.revision = revision;
    saved.config.next_id = next_id > max_id ? next_id : max_id + 1;
    saved.applied_remote_revision = remote_revision;
    for (int i = 0; i < config->count; ++i) saved.fired_minute[i] = fired[i];
    esp_err_t err = save_locked();
    if (err != ESP_OK) saved = previous;
    xSemaphoreGive(mutex);
    return err;
}

void alarm_manager_stop(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    state.ringing = false;
    state.manual = false;
    state.active_id = 0;
    state.ring_until = 0;
    snooze_at = 0;
    manual_fallback = false;
    audio_player_stop();
    xSemaphoreGive(mutex);
}

void alarm_manager_play_now(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    audio_player_set_volume(50);
    state.ringing = false;
    state.manual = true;
    state.active_id = 0;
    state.ring_until = 0;
    manual_fallback = true;
    start_sound_locked(saved.config.manual_sound);
    xSemaphoreGive(mutex);
}

void alarm_manager_play_file(const char *filepath)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    state.ringing = false;
    state.manual = true;
    state.active_id = 0;
    state.ring_until = 0;
    manual_fallback = false;
    audio_player_set_volume(50);
    audio_player_play(filepath);
    xSemaphoreGive(mutex);
}

void alarm_manager_snooze(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    int index = find_index(state.active_id);
    if (state.ringing && index >= 0 && saved.config.items[index].snooze_minutes) {
        snooze_item = saved.config.items[index];
        snooze_at = time(NULL) + snooze_item.snooze_minutes * 60;
        state.ringing = false;
        state.active_id = 0;
        state.ring_until = 0;
        audio_player_stop();
    }
    xSemaphoreGive(mutex);
}

static void scheduler_task(void *arg)
{
    while (1) {
        time_t now = time(NULL);
        xSemaphoreTake(mutex, portMAX_DELAY);
        state.clock_valid = now >= CLOCK_VALID_AFTER;
        state.now = state.clock_valid ? now : 0;
        if (state.ringing && now >= state.ring_until) {
            state.ringing = false;
            state.active_id = 0;
            audio_player_stop();
        }
        if (state.ringing) {
            bool playing;
            char path[4];
            audio_player_get_status(&playing, path, sizeof(path));
            if (!playing) audio_player_play_embedded();
        }
        if (state.manual && manual_fallback) {
            bool playing;
            char path[4];
            audio_player_get_status(&playing, path, sizeof(path));
            if (!playing) {
                audio_player_play_embedded();
                manual_fallback = false;
            }
        }
        if (state.clock_valid) {
            int chosen = -1;
            struct tm local;
            localtime_r(&now, &local);
            int64_t minute = (int64_t)now / 60;
            bool changed = false;
            for (int i = 0; i < saved.config.count; ++i) {
                alarm_item_t *item = &saved.config.items[i];
                if (!item->enabled || item->hour != local.tm_hour || item->minute != local.tm_min ||
                    (item->days && !(item->days & (1U << local.tm_wday))) ||
                    saved.fired_minute[i] == minute) continue;
                saved.fired_minute[i] = minute;
                if (!item->days) item->enabled = false;
                if (chosen < 0 || item->id < saved.config.items[chosen].id) chosen = i;
                changed = true;
            }
            if (changed) { ++saved.config.revision; save_locked(); }
            if (snooze_at && now >= snooze_at) {
                if (chosen < 0) {
                    state.active_id = snooze_item.id;
                    state.ringing = true;
                    state.manual = false;
                    manual_fallback = false;
                    state.ring_until = now + snooze_item.ring_seconds;
                    audio_player_set_volume(snooze_item.volume);
                    start_sound_locked(snooze_item.sound);
                }
                snooze_at = 0;
            }
            if (chosen >= 0) {
                alarm_item_t *item = &saved.config.items[chosen];
                state.active_id = item->id;
                state.ringing = true;
                state.manual = false;
                manual_fallback = false;
                state.ring_until = now + item->ring_seconds;
                audio_player_set_volume(item->volume);
                start_sound_locked(item->sound);
                ESP_LOGI(TAG, "Alarm %lu ringing", (unsigned long)item->id);
            }
        }
        refresh_next_locked(now);
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void button_task(void *arg)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    int stable = 1, previous = 1;
    int samples = 0;
    while (1) {
        int level = gpio_get_level(GPIO_NUM_0);
        if (level == previous) ++samples; else { previous = level; samples = 1; }
        if (samples == 3 && level != stable) {
            stable = level;
            if (stable == 0) {
                bool playing;
                char path[128];
                audio_player_get_status(&playing, path, sizeof(path));
                if (playing) alarm_manager_stop(); else alarm_manager_play_now();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void alarm_manager_start(void)
{
    mutex = xSemaphoreCreateMutex();
    configASSERT(mutex);
    memset(&saved, 0, sizeof(saved));
    saved.version = STORE_VERSION;
    saved.config.next_id = 1;
    for (int i = 0; i < ALARM_MAX; ++i) saved.fired_minute[i] = -1;
    nvs_handle_t handle;
    if (nvs_open("alarm", NVS_READONLY, &handle) == ESP_OK) {
        saved_t *loaded = malloc(sizeof(saved_t));
        size_t size = sizeof(saved_t);
        esp_err_t read_err = loaded ? nvs_get_blob(handle, "config", loaded, &size) : ESP_ERR_NO_MEM;
        if (read_err == ESP_OK && size == sizeof(saved_t) &&
            loaded->version == STORE_VERSION && loaded->config.count <= ALARM_MAX &&
            loaded->crc == crc32(loaded, offsetof(saved_t, crc))) {
            saved = *loaded;
        } else if (read_err == ESP_OK && size == sizeof(saved_v1_t)) {
            saved_v1_t *old = (saved_v1_t *)loaded;
            if (old->version == 1 && old->config.count <= ALARM_MAX &&
                old->crc == crc32(old, offsetof(saved_v1_t, crc))) {
                saved.config = old->config;
                memcpy(saved.fired_minute, old->fired_minute, sizeof(saved.fired_minute));
                ESP_LOGI(TAG, "Migrated M1 alarm schedule to schema 2");
            }
        }
        if (saved.config.count) {
            for (int i = 0; i < saved.config.count; ++i)
                if (saved.config.items[i].ring_seconds > 30)
                    saved.config.items[i].ring_seconds = 30;
            ESP_LOGI(TAG, "Loaded %u alarms, revision %lu", saved.config.count,
                     (unsigned long)saved.config.revision);
        }
        free(loaded);
        nvs_close(handle);
    }
    setenv("TZ", "ICT-7", 1);
    tzset();
    configASSERT(xTaskCreate(scheduler_task, "alarm_scheduler", 6144, NULL, 5, NULL) == pdPASS);
    configASSERT(xTaskCreate(button_task, "alarm_button", 3072, NULL, 5, NULL) == pdPASS);
}
