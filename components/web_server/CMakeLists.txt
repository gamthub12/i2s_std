idf_component_register(SRCS "web_server.c" "alarm_web.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_http_server esp_event esp_netif heap sd_card audio_player alarm_manager firebase_client lwip
                    EMBED_TXTFILES "alarms.html")
