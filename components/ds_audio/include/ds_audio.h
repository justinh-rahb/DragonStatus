#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/* OEM Status adapter.  The generic capture/filter interface is intentionally
 * separate from this board's pin mux so it can move to dragon-core unchanged. */
esp_err_t ds_audio_start(void);
float ds_audio_get_level(void);

/* Keep transport details out of the portal while exposing enough state to
 * diagnose a new board adapter over the network. */
typedef struct {
    bool codec_ready;
    esp_err_t codec_error;
    esp_err_t capture_error;
    uint8_t capture_stage;
    size_t capture_bytes;
    float raw_level;
} ds_audio_status_t;

void ds_audio_get_status(ds_audio_status_t *status);
