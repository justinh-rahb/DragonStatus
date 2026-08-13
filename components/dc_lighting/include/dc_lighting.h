#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

/* Board-neutral WS2812 renderer.  Products own GPIO mapping, state policy, and
 * persistence; this owns output topology, frame timing, brightness and effects.
 * It is deliberately local until its first DragonVent consumer lands, then moves
 * unchanged to dragon-core/components/dc_lighting. */

#define DC_LIGHTING_MAX_OUTPUTS 2

typedef enum {
    DC_LIGHTING_SOLID = 0,
    DC_LIGHTING_BREATHE,
    DC_LIGHTING_RAINBOW,
    DC_LIGHTING_BLINK,
    DC_LIGHTING_FLOW,
    DC_LIGHTING_PROGRESS,
    DC_LIGHTING_CYLON,
} dc_lighting_effect_t;

typedef struct { uint8_t r, g, b; } dc_rgb_t;

typedef struct {
    gpio_num_t gpio;
    uint16_t pixels;
    bool reverse;
} dc_lighting_output_t;

typedef struct {
    const dc_lighting_output_t *outputs;
    uint8_t output_count;
    uint8_t brightness;
    uint8_t fps;
} dc_lighting_config_t;

esp_err_t dc_lighting_start(const dc_lighting_config_t *config);
esp_err_t dc_lighting_set(dc_rgb_t color, dc_lighting_effect_t effect, uint8_t speed);
// Progress is normalized to 0..1.  A negative value means unavailable.
esp_err_t dc_lighting_set_progress(float progress);
esp_err_t dc_lighting_off(void);
