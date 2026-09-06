#pragma once

#include "ds_lighting.h"
#include "esp_err.h"

/* Start DragonStatus's read-only Dragon-family discovery publisher. */
esp_err_t ds_peer_start(void);

/* Refresh the advisory printer/lighting snapshot published on the heartbeat. */
void ds_peer_update(ds_printer_state_t state, float progress);
