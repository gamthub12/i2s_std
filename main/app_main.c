#include "audio_player.h"
#include "microphone.h"
#include "sd_card.h"
#include "web_server.h"
#include "sdkconfig.h"
#include "alarm_manager.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "firebase_client.h"

void app_main(void)
{
    /* Start SD detection before services that may access /sdcard. */
    sd_card_init();
    audio_player_start();
    /* Alarm schedules live in NVS. Never erase that partition automatically. */
    ESP_ERROR_CHECK(nvs_flash_init());
    alarm_manager_start();
#if CONFIG_AUDIO_ENABLE_MIC
    microphone_init();
#endif
    web_server_start();
#if CONFIG_M2_FIREBASE_ENABLED
    firebase_client_start();
#endif
}
