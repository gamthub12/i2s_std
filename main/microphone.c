#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "i2s_example_pins.h"
#include "microphone.h"
#include "audio_player.h"
#include "sd_card.h"

static const char *TAG = "microphone";

/* Use separate I2S controller for mic to avoid sharing pins with speaker */
#define MIC_I2S_NUM        I2S_NUM_1
#define MIC_SAMPLE_RATE    16000
#define MIC_BIT_WIDTH      I2S_DATA_BIT_WIDTH_16BIT
#define MIC_SLOT_MODE      I2S_SLOT_MODE_MONO
#define MIC_RECORD_MS      6000   /* Max 6 seconds recording */
#define MIC_DMA_DESC_NUM   8
#define MIC_DMA_FRAME_NUM  256

#define BUTTON_IO          GPIO_NUM_0

static i2s_chan_handle_t mic_rx_chan;
static TaskHandle_t mic_task_handle = NULL;

static volatile bool button_pressed = false;
static volatile bool record_requested = false;

static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t level = gpio_get_level(BUTTON_IO);
    if (level == 0) {
        button_pressed = true;
        record_requested = true;
    } else {
        button_pressed = false;
    }
}

static bool write_wav_header(FILE *file, uint32_t data_size)
{
    const uint32_t sample_rate = MIC_SAMPLE_RATE;
    const uint32_t byte_rate = MIC_SAMPLE_RATE * sizeof(int16_t);
    const uint32_t riff_size = 36 + data_size;
    const uint32_t format_size = 16;
    const uint16_t format = 1;
    const uint16_t channels = 1;
    const uint16_t block_align = sizeof(int16_t);
    const uint16_t bits_per_sample = 16;

    return fwrite("RIFF", 1, 4, file) == 4 && fwrite(&riff_size, 1, 4, file) == 4 &&
           fwrite("WAVEfmt ", 1, 8, file) == 8 && fwrite(&format_size, 1, 4, file) == 4 &&
           fwrite(&format, 1, 2, file) == 2 && fwrite(&channels, 1, 2, file) == 2 &&
           fwrite(&sample_rate, 1, 4, file) == 4 && fwrite(&byte_rate, 1, 4, file) == 4 &&
           fwrite(&block_align, 1, 2, file) == 2 && fwrite(&bits_per_sample, 1, 2, file) == 2 &&
           fwrite("data", 1, 4, file) == 4 && fwrite(&data_size, 1, 4, file) == 4;
}

static void microphone_task(void *arg)
{
    ESP_LOGI(TAG, "Microphone task started. Hold BOOT (IO0) to record, release to play back.");

    /* Configure GPIO 0 - BOOT button (active LOW) */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_IO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* Install ISR service and add handler */
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_IO, button_isr_handler, NULL));

    /* Allocate recording buffer (max 6 seconds) */
    size_t max_samples = (MIC_SAMPLE_RATE * MIC_RECORD_MS) / 1000;
    size_t buf_bytes = max_samples * sizeof(int16_t);
    int16_t *rec_buf = (int16_t *)calloc(1, buf_bytes);
    assert(rec_buf);

    size_t recorded_samples = 0;

    /* Allocate temporary chunk buffer */
    size_t chunk_samples = 1024;
    uint8_t *chunk_buf = (uint8_t *)calloc(1, chunk_samples * sizeof(int16_t));
    assert(chunk_buf);

    /* Initialize I2S1 as RX (mic input) with separate pins */
    i2s_chan_config_t rx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_NUM, I2S_ROLE_MASTER);
    rx_cfg.dma_desc_num = MIC_DMA_DESC_NUM;
    rx_cfg.dma_frame_num = MIC_DMA_FRAME_NUM;
    ESP_ERROR_CHECK(i2s_new_channel(&rx_cfg, NULL, &mic_rx_chan));

    i2s_std_config_t mic_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(MIC_BIT_WIDTH, MIC_SLOT_MODE),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = EXAMPLE_I2S_BCLK_IO2,   /* GPIO 6 */
            .ws   = EXAMPLE_I2S_WS_IO2,     /* GPIO 7 */
            .dout = I2S_GPIO_UNUSED,
            .din  = EXAMPLE_I2S_DIN_IO2,    /* GPIO 9 */
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(mic_rx_chan, &mic_std_cfg));

    while (1) {
        /* Idle: wait for button press */
        if (!record_requested) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        ESP_LOGI(TAG, ">>> Recording started (hold button)...");

        /* Reset buffer */
        recorded_samples = 0;
        ESP_ERROR_CHECK(i2s_channel_enable(mic_rx_chan));

        /* Recording loop: read while button is held */
        while (button_pressed && recorded_samples < max_samples) {
            size_t bytes_read = 0;
            esp_err_t ret = i2s_channel_read(mic_rx_chan, chunk_buf,
                                             chunk_samples * sizeof(int16_t),
                                             &bytes_read, pdMS_TO_TICKS(500));
            if (ret == ESP_OK && bytes_read > 0) {
                size_t samples_copied = bytes_read / sizeof(int16_t);
                size_t space_left = max_samples - recorded_samples;
                if (samples_copied > space_left) {
                    samples_copied = space_left;
                }
                memcpy((uint8_t *)rec_buf + recorded_samples * sizeof(int16_t),
                       chunk_buf, samples_copied * sizeof(int16_t));
                recorded_samples += samples_copied;
            }
        }

        /* Stop recording */
        i2s_channel_disable(mic_rx_chan);
        record_requested = false;

        ESP_LOGI(TAG, ">>> Recording stopped. Captured %u samples (%.2f seconds)",
                 (unsigned)recorded_samples,
                 (float)recorded_samples / MIC_SAMPLE_RATE);

        if (recorded_samples == 0) {
            ESP_LOGW(TAG, "No audio captured, skipping playback");
            continue;
        }

        ESP_LOGI(TAG, ">>> Saving and playing recording...");
        uint32_t data_size = recorded_samples * sizeof(int16_t);
        if (!sd_card_lock()) {
            ESP_LOGE(TAG, "Cannot save recording: SD card is not mounted");
            continue;
        }
        FILE *f = fopen("/sdcard/recorded.wav", "wb");
        if (f != NULL) {
            bool saved = write_wav_header(f, data_size) &&
                         fwrite(rec_buf, 1, data_size, f) == data_size;
            saved = (fclose(f) == 0) && saved;
            if (!saved) unlink("/sdcard/recorded.wav");
            sd_card_unlock();
            if (saved) {
                ESP_LOGI(TAG, "Saved recording to /sdcard/recorded.wav");
                audio_player_play("/sdcard/recorded.wav");
                vTaskDelay(pdMS_TO_TICKS((recorded_samples * 1000) / MIC_SAMPLE_RATE + 1000));
            } else {
                ESP_LOGE(TAG, "Failed to write recording to SD card");
            }
        } else {
            sd_card_unlock();
            ESP_LOGE(TAG, "Failed to save recording to SD card");
        }

        ESP_LOGI(TAG, "<<< Playback finished. Press button again to record.");
    }

    free(rec_buf);
    free(chunk_buf);
    vTaskDelete(NULL);
}

void microphone_init(void)
{
    ESP_LOGI(TAG, "Creating microphone task...");
    xTaskCreate(microphone_task, "microphone_task", 8192, NULL, 5, &mic_task_handle);
}
