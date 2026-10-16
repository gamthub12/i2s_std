idf_component_register(
    SRCS "app_main.c" "microphone.c"
    PRIV_REQUIRES esp_driver_i2s esp_driver_gpio i2s_examples_common audio_player sd_card web_server alarm_manager firebase_client nvs_flash
    INCLUDE_DIRS "."
)
