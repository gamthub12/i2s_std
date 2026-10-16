#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#define ALARM_MAX 20
#define ALARM_NAME_MAX 32
#define ALARM_SOUND_MAX 96

typedef struct {
    uint32_t id;
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    uint8_t days; /* Sunday bit 0 through Saturday bit 6; zero means once */
    uint8_t volume; /* 0..100 */
    uint16_t ring_seconds;
    uint8_t snooze_minutes;
    char name[ALARM_NAME_MAX];
    char sound[ALARM_SOUND_MAX]; /* SD basename; empty means embedded sound */
} alarm_item_t;

typedef struct {
    uint32_t revision;
    uint32_t next_id;
    uint8_t count;
    char manual_sound[ALARM_SOUND_MAX];
    alarm_item_t items[ALARM_MAX];
} alarm_config_t;

typedef struct {
    bool clock_valid;
    time_t now;
    time_t next_at;
    uint32_t next_id;
    uint32_t active_id;
    bool ringing;
    bool manual;
    time_t ring_until;
} alarm_state_t;

void alarm_manager_start(void);
void alarm_manager_get(alarm_config_t *config, alarm_state_t *state);
esp_err_t alarm_manager_put(const alarm_item_t *item, uint32_t *id);
esp_err_t alarm_manager_delete(uint32_t id);
esp_err_t alarm_manager_set_manual_sound(const char *sound);
esp_err_t alarm_manager_apply_remote(const alarm_config_t *config, uint32_t remote_revision);
uint32_t alarm_manager_remote_revision(void);
void alarm_manager_play_now(void);
void alarm_manager_play_file(const char *filepath);
void alarm_manager_stop(void);
void alarm_manager_snooze(void);
