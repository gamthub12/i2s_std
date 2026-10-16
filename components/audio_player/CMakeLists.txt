idf_component_register(
    SRCS "audio_player.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2s i2s_examples_common sd_card
    EMBED_FILES ../../data/audio.wav
)
