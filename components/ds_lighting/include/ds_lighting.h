#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    DS_PRINTER_UNKNOWN, DS_PRINTER_IDLE, DS_PRINTER_PREPARING, DS_PRINTER_PRINTING,
    DS_PRINTER_PAUSED, DS_PRINTER_COMPLETE, DS_PRINTER_ERROR,
} ds_printer_state_t;

/* Stable product-side subset of DragonVent's lighting resource.  The generic
 * renderer owns animation; Status owns these OEM-policy defaults. */
typedef struct {
    bool enabled;
    uint8_t brightness;
    uint8_t speed;
    /* 0 retains the OEM H2D state policy; any other value applies one
     * dc_lighting_effect_t to every state using printing_color. */
    uint8_t effect;
    /* OEM H2D exposes three semantic palette entries.  The documented
     * unbound/preparing/completed colours remain factory-owned. */
    uint8_t idle_color[3];
    uint8_t printing_color[3];
    uint8_t error_color[3];
} ds_lighting_config_t;

esp_err_t ds_lighting_start(void);
void ds_lighting_update(ds_printer_state_t state, float progress);
void ds_lighting_get_config(ds_lighting_config_t *out);
esp_err_t ds_lighting_set_config(const ds_lighting_config_t *config);
