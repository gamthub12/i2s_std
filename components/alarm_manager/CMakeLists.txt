idf_component_register(SRCS "alarm_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_player sd_card nvs_flash esp_driver_gpio)
