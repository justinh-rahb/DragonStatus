#include "dc_lighting.h"

#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "dc_lighting";
static led_strip_handle_t s_strip[DC_LIGHTING_MAX_OUTPUTS];
static dc_lighting_output_t s_output[DC_LIGHTING_MAX_OUTPUTS];
static uint8_t s_count, s_brightness = 255, s_fps = 30, s_speed;
static float s_progress = -1.0f;
static dc_rgb_t s_color;
static dc_lighting_effect_t s_effect;
static SemaphoreHandle_t s_lock;

static uint8_t scale(uint8_t value, uint8_t level) { return (uint16_t)value * level / 255; }

static void hsv(uint16_t hue, dc_rgb_t *out)
{
    uint8_t region = hue / 10923;
    uint16_t rem = (uint16_t)((hue - region * 10923) * 6);
    uint8_t q = 255 - rem / 257;
    uint8_t t = rem / 257;
    switch (region) {
    case 0: *out = (dc_rgb_t){255, t, 0}; break;
    case 1: *out = (dc_rgb_t){q, 255, 0}; break;
    case 2: *out = (dc_rgb_t){0, 255, t}; break;
    case 3: *out = (dc_rgb_t){0, q, 255}; break;
    case 4: *out = (dc_rgb_t){t, 0, 255}; break;
    default:*out = (dc_rgb_t){255, 0, q}; break;
    }
}

static void frame(uint32_t tick)
{
    dc_rgb_t base = s_color;
    uint8_t level = s_brightness;
    if (s_effect == DC_LIGHTING_BREATHE) {
        uint16_t phase = (tick * (uint32_t)(s_speed + 1)) & 511;
        uint8_t wave = phase < 256 ? phase : 511 - phase;
        level = scale(level, 40 + (uint16_t)wave * 215 / 255);
    } else if (s_effect == DC_LIGHTING_BLINK && ((tick * (uint32_t)(s_speed + 1) / 10) & 1)) {
        level = 0;
    }
    for (uint8_t i = 0; i < s_count; ++i) {
        for (uint16_t p = 0; p < s_output[i].pixels; ++p) {
            dc_rgb_t c = base;
            if (s_effect == DC_LIGHTING_RAINBOW) hsv((uint16_t)(tick * (s_speed + 1) * 32 + p * (65536 / s_output[i].pixels)), &c);
            if (s_effect == DC_LIGHTING_FLOW) {
                uint16_t head = (tick * (uint32_t)(s_speed + 1) / 12) % s_output[i].pixels;
                uint16_t distance = (p + s_output[i].pixels - head) % s_output[i].pixels;
                uint8_t tail = distance < 6 ? (uint8_t)((6 - distance) * 255 / 6) : 0;
                c = base;
                level = scale(s_brightness, tail);
            }
            if (s_effect == DC_LIGHTING_PROGRESS) {
                bool lit = s_progress >= 0.0f && p < (uint16_t)(s_progress * s_output[i].pixels);
                c = lit ? base : (dc_rgb_t){0, 0, 0};
                level = lit ? s_brightness : 255;
            }
            uint16_t index = s_output[i].reverse ? s_output[i].pixels - 1 - p : p;
            led_strip_set_pixel(s_strip[i], index, scale(c.r, level), scale(c.g, level), scale(c.b, level));
        }
        led_strip_refresh(s_strip[i]);
    }
}

static void render_task(void *arg)
{
    (void)arg;
    for (uint32_t tick = 0;; ++tick) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        frame(tick);
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(1000 / s_fps));
    }
}

esp_err_t dc_lighting_start(const dc_lighting_config_t *config)
{
    if (!config || !config->outputs || !config->output_count || config->output_count > DC_LIGHTING_MAX_OUTPUTS || !config->fps) return ESP_ERR_INVALID_ARG;
    if (s_lock) return ESP_ERR_INVALID_STATE;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    s_brightness = config->brightness;
    s_fps = config->fps;
    for (uint8_t i = 0; i < config->output_count; ++i) {
        if (config->outputs[i].gpio == GPIO_NUM_NC || !config->outputs[i].pixels) return ESP_ERR_INVALID_ARG;
        led_strip_config_t strip = { .strip_gpio_num = config->outputs[i].gpio, .max_leds = config->outputs[i].pixels, .led_pixel_format = LED_PIXEL_FORMAT_GRB, .led_model = LED_MODEL_WS2812 };
        esp_err_t err = led_strip_new_rmt_device(&strip, &(led_strip_rmt_config_t){ .resolution_hz = 10 * 1000 * 1000 }, &s_strip[i]);
        if (err != ESP_OK) return err;
        s_output[i] = config->outputs[i];
        ++s_count;
    }
    ESP_LOGI(TAG, "started %u WS2812 output(s)", s_count);
    return xTaskCreate(render_task, "dc_lighting", 3072, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t dc_lighting_set(dc_rgb_t color, dc_lighting_effect_t effect, uint8_t speed)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY); s_color = color; s_effect = effect; s_speed = speed; xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t dc_lighting_set_progress(float progress)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (progress > 1.0f) progress = 1.0f;
    xSemaphoreTake(s_lock, portMAX_DELAY); s_progress = progress; xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t dc_lighting_off(void) { return dc_lighting_set((dc_rgb_t){0, 0, 0}, DC_LIGHTING_SOLID, 0); }
