idf_component_register(SRCS "firebase_client.c" "json_tree.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp-tls mbedtls nvs_flash esp_wifi esp_timer alarm_manager sd_card audio_player)
