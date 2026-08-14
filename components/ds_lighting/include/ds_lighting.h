#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    DS_PRINTER_UNKNOWN, DS_PRINTER_IDLE, DS_PRINTER_PREPARING, DS_PRINTER_PRINTING,
    DS_PRINTER_PAUSED, DS_PRINTER_COMPLETE, DS_PRINTER_ERROR,
} ds_printer_state_t;

/* Product-facing IDs deliberately do not mirror dc_lighting_effect_t: zero
 * means "follow factory H2D policy", while Core's zero is Solid. */
typedef enum {
    DS_LIGHTING_FACTORY_H2D = 0,
    DS_LIGHTING_SOLID = 1,
    DS_LIGHTING_BREATHE = 2,
    DS_LIGHTING_RAINBOW = 3,
    DS_LIGHTING_BLINK = 4,
    DS_LIGHTING_FLOW = 5,
    DS_LIGHTING_CYLON = 6,
    DS_LIGHTING_MUSIC = 7,
    DS_LIGHTING_CYCLE = 8,
    DS_LIGHTING_WAVE = 9,
    DS_LIGHTING_MARQUEE = 10,
} ds_lighting_effect_t;

/* Stable product-side subset of DragonVent's lighting resource.  The generic
 * renderer owns animation; Status owns these OEM-policy defaults. */
typedef struct {
    bool enabled;
    uint8_t brightness;
    uint8_t speed;
    /* 0 retains OEM H2D policy; nonzero is a ds_lighting_effect_t override. */
    uint8_t effect;
    /* OEM H2D exposes three semantic palette entries.  The documented
     * unbound/preparing/completed colours remain factory-owned. */
    uint8_t idle_color[3];
    uint8_t printing_color[3];
    uint8_t error_color[3];
} ds_lighting_config_t;

esp_err_t ds_lighting_start(void);
void ds_lighting_update(ds_printer_state_t state, float progress);
/* Board audio adapter supplies its filtered 0..1 level here. */
void ds_lighting_set_audio_level(float level);
void ds_lighting_get_config(ds_lighting_config_t *out);
esp_err_t ds_lighting_set_config(const ds_lighting_config_t *config);
