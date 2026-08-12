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
    uint8_t effect; /* 0 = factory state effect; otherwise dc_lighting_effect_t */
} ds_lighting_config_t;

esp_err_t ds_lighting_start(void);
void ds_lighting_update(ds_printer_state_t state, float progress);
void ds_lighting_get_config(ds_lighting_config_t *out);
esp_err_t ds_lighting_set_config(const ds_lighting_config_t *config);
