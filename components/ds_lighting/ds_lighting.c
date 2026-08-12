#include "ds_lighting.h"
#include "ds_board.h"
#include "dc_lighting.h"
#include "esp_log.h"

static const char *TAG = "ds_lighting";

esp_err_t ds_lighting_start(void)
{
    dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS]; uint8_t count = 0;
    if (!ds_board_lighting_outputs(outputs, &count)) {
        ESP_LOGW(TAG, "RGB output disabled until board pinout is verified");
        return ESP_OK;
    }
    return dc_lighting_start(&(dc_lighting_config_t){ .outputs = outputs, .output_count = count, .brightness = 128, .fps = 30 });
}

void ds_lighting_update(ds_printer_state_t state, float progress)
{
    dc_rgb_t color = {255, 255, 255}; dc_lighting_effect_t fx = DC_LIGHTING_SOLID;
    switch (state) {
    case DS_PRINTER_UNKNOWN:   color = (dc_rgb_t){0, 80, 255}; fx = DC_LIGHTING_FLOW; break;
    case DS_PRINTER_PREPARING: color = (dc_rgb_t){248, 163, 35}; fx = DC_LIGHTING_FLOW; break;
    case DS_PRINTER_PRINTING:  color = (dc_rgb_t){255, 255, 255}; fx = progress >= 0.0f ? DC_LIGHTING_PROGRESS : DC_LIGHTING_SOLID; break;
    case DS_PRINTER_PAUSED:    color = (dc_rgb_t){255, 255, 255}; fx = DC_LIGHTING_BREATHE; break;
    case DS_PRINTER_COMPLETE:  color = (dc_rgb_t){0, 255, 42}; break;
    case DS_PRINTER_ERROR:     color = (dc_rgb_t){255, 0, 0}; fx = DC_LIGHTING_BLINK; break;
    case DS_PRINTER_IDLE:      color = (dc_rgb_t){255, 255, 255}; fx = DC_LIGHTING_BREATHE; break;
    default: break;
    }
    (void)dc_lighting_set_progress(progress);
    (void)dc_lighting_set(color, fx, 96);
}
