#include "ds_lighting.h"
#include "ds_board.h"
#include "dc_lighting.h"
#include "esp_log.h"
#include "nvs.h"

#define DS_LIGHT_NVS_NS "app_nvs"
#define DS_LIGHT_NVS_KEY "ds_lighting"
static ds_lighting_config_t s_config = {
    .enabled = true,
    /* OEM H2D slot reports 25%; retain the original appearance for a fresh
     * DragonStatus install while allowing the user to tune it later. */
    .brightness = 64,
    .speed = 96,
    .idle_color = {255, 255, 255},
    .printing_color = {255, 255, 255},
    .error_color = {255, 0, 0},
};

void ds_lighting_get_config(ds_lighting_config_t *out) { if (out) *out = s_config; }

esp_err_t ds_lighting_set_config(const ds_lighting_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    nvs_handle_t nvs;
    if (nvs_open(DS_LIGHT_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return ESP_FAIL;
    esp_err_t err = nvs_set_blob(nvs, DS_LIGHT_NVS_KEY, &s_config, sizeof(s_config));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static const char *TAG = "ds_lighting";

esp_err_t ds_lighting_start(void)
{
    nvs_handle_t nvs; size_t size = sizeof(s_config);
    if (nvs_open(DS_LIGHT_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        (void)nvs_get_blob(nvs, DS_LIGHT_NVS_KEY, &s_config, &size); nvs_close(nvs);
    }
    dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS]; uint8_t count = 0;
    if (!ds_board_lighting_outputs(outputs, &count)) {
        ESP_LOGW(TAG, "RGB output disabled until board pinout is verified");
        return ESP_OK;
    }
    return dc_lighting_start(&(dc_lighting_config_t){ .outputs = outputs, .output_count = count, .brightness = s_config.brightness, .fps = 30 });
}

void ds_lighting_update(ds_printer_state_t state, float progress)
{
    if (!s_config.enabled) { (void)dc_lighting_off(); return; }
    dc_rgb_t idle = {s_config.idle_color[0], s_config.idle_color[1], s_config.idle_color[2]};
    dc_rgb_t printing = {s_config.printing_color[0], s_config.printing_color[1], s_config.printing_color[2]};
    dc_rgb_t error = {s_config.error_color[0], s_config.error_color[1], s_config.error_color[2]};
    dc_rgb_t color = idle; dc_lighting_effect_t fx = DC_LIGHTING_SOLID;
    switch (state) {
    case DS_PRINTER_UNKNOWN:   color = (dc_rgb_t){0, 80, 255}; fx = DC_LIGHTING_FLOW; break;
    case DS_PRINTER_PREPARING: color = (dc_rgb_t){248, 163, 35}; fx = DC_LIGHTING_FLOW; break;
    case DS_PRINTER_PRINTING:  color = printing; fx = progress >= 0.0f ? DC_LIGHTING_PROGRESS : DC_LIGHTING_SOLID; break;
    case DS_PRINTER_PAUSED:    color = idle; fx = DC_LIGHTING_BREATHE; break;
    case DS_PRINTER_COMPLETE:  color = (dc_rgb_t){0, 255, 42}; break;
    case DS_PRINTER_ERROR:     color = error; fx = DC_LIGHTING_BLINK; break;
    case DS_PRINTER_IDLE:      color = idle; fx = DC_LIGHTING_BREATHE; break;
    default: break;
    }
    (void)dc_lighting_set_progress(progress);
    if (s_config.effect) {
        fx = (dc_lighting_effect_t)s_config.effect;
        color = fx == DC_LIGHTING_CYLON ? (dc_rgb_t){255, 0, 0} : printing;
    }
    (void)dc_lighting_set(color, fx, s_config.speed);
}

void ds_lighting_set_audio_level(float level)
{
    (void)dc_lighting_set_audio_level(level);
}
