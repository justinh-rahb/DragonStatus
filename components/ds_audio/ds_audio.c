#include "ds_audio.h"

#include "dc_lighting.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "ds_lighting.h"
#include "es8311.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Recovered from OEM board_pins_config.c:
 * get_i2c_pins: SDA=2, SCL=3
 * get_i2s_pins(0) fills ESP-IDF's legacy i2s_pin_config_t, whose order is
 * MCLK, BCLK, WS, DOUT, DIN: MCLK=0, BCLK=1, WS=4, DOUT=7, DIN=10. */
#define DS_AUDIO_MCLK GPIO_NUM_0
#define DS_AUDIO_BCLK GPIO_NUM_1
#define DS_AUDIO_WS GPIO_NUM_4
#define DS_AUDIO_DOUT GPIO_NUM_7
#define DS_AUDIO_DIN GPIO_NUM_10
#define DS_AUDIO_SDA GPIO_NUM_2
#define DS_AUDIO_SCL GPIO_NUM_3
/* The stock ADF audio stream's default is 16 kHz.  Music mode needs a
 * responsive level meter, not playback fidelity, and this keeps the codec's
 * serial clock in its known-good capture profile. */
#define DS_AUDIO_RATE_HZ 16000
#define DS_AUDIO_SAMPLES 512

static const char *TAG = "ds_audio";
static volatile float s_level;
static es8311_handle_t s_codec;
static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;
static volatile bool s_codec_ready;
static volatile esp_err_t s_codec_error = ESP_ERR_INVALID_STATE;
static volatile esp_err_t s_capture_error = ESP_ERR_INVALID_STATE;
static volatile uint8_t s_capture_stage;
static volatile size_t s_capture_bytes;
static volatile float s_raw_level;

static esp_err_t i2s_start(void)
{
    if (s_rx) return ESP_OK;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 3;
    channel.dma_frame_num = 128;
    channel.auto_clear = true;
    s_capture_stage = 1;
    esp_err_t err = i2s_new_channel(&channel, &s_tx, &s_rx);
    if (err != ESP_OK) return err;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DS_AUDIO_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = DS_AUDIO_MCLK,
            .bclk = DS_AUDIO_BCLK,
            .ws = DS_AUDIO_WS,
            .dout = DS_AUDIO_DOUT,
            .din = DS_AUDIO_DIN,
        },
    };
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    s_capture_stage = 2;
    err = i2s_channel_init_std_mode(s_tx, &config);
    if (err == ESP_OK) err = i2s_channel_init_std_mode(s_rx, &config);
    if (err == ESP_OK) { s_capture_stage = 3; err = i2s_channel_enable(s_tx); }
    if (err == ESP_OK) err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        if (s_rx) (void)i2s_del_channel(s_rx);
        if (s_tx) (void)i2s_del_channel(s_tx);
        s_rx = NULL; s_tx = NULL;
    }
    return err;
}

static esp_err_t codec_start(void)
{
    if (s_codec) return ESP_OK;
    const i2c_config_t i2c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = DS_AUDIO_SDA,
        .scl_io_num = DS_AUDIO_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t err = i2c_param_config(I2C_NUM_0, &i2c);
    if (err != ESP_OK) goto failed;
    err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto failed;
    s_codec = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
    if (!s_codec) { err = ESP_ERR_NOT_FOUND; goto failed; }
    const es8311_clock_config_t clock = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = DS_AUDIO_RATE_HZ * 256,
        .sample_frequency = DS_AUDIO_RATE_HZ,
    };
    err = es8311_init(s_codec, &clock, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err == ESP_OK) err = es8311_sample_frequency_config(s_codec, DS_AUDIO_RATE_HZ * 256, DS_AUDIO_RATE_HZ);
    if (err == ESP_OK) err = es8311_microphone_config(s_codec, false);
    /* API expects the gain enum, not a raw dB integer.  The latter programs
     * reserved ADC bits and leaves the mic path silent on Status hardware. */
    if (err == ESP_OK) err = es8311_microphone_gain_set(s_codec, ES8311_MIC_GAIN_36DB);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 init: %s", esp_err_to_name(err));
        es8311_delete(s_codec);
        s_codec = NULL;
        goto failed;
    }
    s_codec_ready = true;
    s_codec_error = ESP_OK;
    return err;

failed:
    s_codec_ready = false;
    s_codec_error = err;
    return err;
}

static bool music_enabled(void)
{
    ds_lighting_config_t lighting;
    ds_lighting_get_config(&lighting);
    return lighting.enabled && lighting.effect == DC_LIGHTING_AUDIO_METER;
}

static esp_err_t capture_level(float *out)
{
    int16_t samples[DS_AUDIO_SAMPLES];
    size_t got = 0;
    s_capture_stage = 4;
    esp_err_t err = i2s_channel_read(s_rx, samples, sizeof(samples), &got, pdMS_TO_TICKS(80));
    s_capture_bytes = got;
    if (err != ESP_OK || got < sizeof(int16_t) * 16) {
        s_capture_error = err == ESP_OK ? ESP_ERR_TIMEOUT : err;
        return s_capture_error;
    }

    uint32_t sum = 0;
    size_t count = got / sizeof(samples[0]);
    for (size_t i = 0; i < count; ++i) {
        int32_t value = samples[i];
        sum += (uint32_t)(value < 0 ? -value : value);
    }
    /* ES8311 PCM has a small idle floor; map normal room level into the OEM
     * meter's useful 0..1 range without turning silence into a full bar. */
    float average = (float)sum / (float)count / 32768.0f;
    s_raw_level = average;
    *out = average <= 0.012f ? 0.0f : (average - 0.012f) * 8.0f;
    if (*out > 1.0f) *out = 1.0f;
    s_capture_error = ESP_OK;
    s_capture_stage = 0;
    return ESP_OK;
}

static void audio_task(void *arg)
{
    (void)arg;
    float smooth = 0.0f;
    for (;;) {
        if (!music_enabled()) {
            smooth = 0.0f;
            s_level = 0.0f;
            ds_lighting_set_audio_level(0.0f);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!s_codec) {
            esp_err_t i2s_err = i2s_start();
            if (i2s_err != ESP_OK) {
                s_capture_error = i2s_err;
                ESP_LOGW(TAG, "I2S unavailable: %s", esp_err_to_name(i2s_err));
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            esp_err_t init_err = codec_start();
            if (init_err != ESP_OK) {
                ESP_LOGW(TAG, "ES8311 unavailable: %s", esp_err_to_name(init_err));
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }
        float level = 0.0f;
        esp_err_t err = capture_level(&level);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S capture: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        /* Fast attack, gentle decay: readable but reactive to beats. */
        smooth = level > smooth ? smooth * 0.35f + level * 0.65f : smooth * 0.82f + level * 0.18f;
        ds_lighting_set_audio_level(smooth);
        s_level = smooth;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

float ds_audio_get_level(void) { return s_level; }

void ds_audio_get_status(ds_audio_status_t *status)
{
    if (!status) return;
    *status = (ds_audio_status_t){
        .codec_ready = s_codec_ready,
        .codec_error = s_codec_error,
        .capture_error = s_capture_error,
        .capture_stage = s_capture_stage,
        .capture_bytes = s_capture_bytes,
        .raw_level = s_raw_level,
    };
}

esp_err_t ds_audio_start(void)
{
    return xTaskCreate(audio_task, "ds_audio", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
