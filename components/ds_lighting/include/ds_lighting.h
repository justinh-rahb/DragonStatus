#pragma once
#include "esp_err.h"

typedef enum {
    DS_PRINTER_UNKNOWN, DS_PRINTER_IDLE, DS_PRINTER_PREPARING, DS_PRINTER_PRINTING,
    DS_PRINTER_PAUSED, DS_PRINTER_COMPLETE, DS_PRINTER_ERROR,
} ds_printer_state_t;

esp_err_t ds_lighting_start(void);
void ds_lighting_update(ds_printer_state_t state, float progress);
