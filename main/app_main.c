#include "dc_bambu.h"
#include "dc_evlog.h"
#include "dc_moonraker.h"
#include "dc_source.h"
#include "dc_wifi.h"
#include "ds_lighting.h"
#include "ds_audio.h"
#include "ds_portal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

static ds_printer_state_t bambu_printer_state(const dc_bambu_status_t *status)
{
    switch (status->print_state) {
    case DC_BAMBU_PRINT_IDLE:      return DS_PRINTER_IDLE;
    case DC_BAMBU_PRINT_DOWNLOADING: return DS_PRINTER_DOWNLOADING;
    case DC_BAMBU_PRINT_PREPARING: return DS_PRINTER_PREPARING;
    case DC_BAMBU_PRINT_PRINTING:  return DS_PRINTER_PRINTING;
    case DC_BAMBU_PRINT_PAUSED:    return DS_PRINTER_PAUSED;
    case DC_BAMBU_PRINT_COMPLETE:  return DS_PRINTER_COMPLETE;
    case DC_BAMBU_PRINT_ERROR:     return DS_PRINTER_ERROR;
    default:
        /* MQTT report deltas frequently omit gcode_state.  Core preserves the
         * active/error flags across those deltas, so keep driving the last
         * known print policy until a complete phase-bearing report arrives. */
        return status->error ? DS_PRINTER_ERROR : status->printing ? DS_PRINTER_PRINTING :
               status->connected ? DS_PRINTER_IDLE : DS_PRINTER_UNKNOWN;
    }
}

static ds_printer_state_t moonraker_printer_state(dc_printer_state_t state)
{
    switch (state) {
    case DC_PRINTER_IDLE:      return DS_PRINTER_IDLE;
    case DC_PRINTER_PREPARING: return DS_PRINTER_PREPARING;
    case DC_PRINTER_PRINTING:  return DS_PRINTER_PRINTING;
    case DC_PRINTER_PAUSED:    return DS_PRINTER_PAUSED;
    case DC_PRINTER_COMPLETE:  return DS_PRINTER_COMPLETE;
    case DC_PRINTER_ERROR:     return DS_PRINTER_ERROR;
    default:                   return DS_PRINTER_UNKNOWN;
    }
}

static void update_lighting(void)
{
    ds_printer_state_t state = DS_PRINTER_UNKNOWN;
    float progress = -1.0f;
    if (dc_source_get() == DC_SRC_BAMBU) {
        dc_bambu_status_t s = {0}; dc_bambu_get_status(&s);
        state = bambu_printer_state(&s);
        progress = s.progress;
    } else if (dc_source_get() == DC_SRC_KLIPPER) {
        dc_moonraker_status_t s = {0}; dc_moonraker_get_status(&s);
        state = moonraker_printer_state(s.printer);
        progress = s.progress;
    }
    ds_lighting_update(state, progress);
}

/* The stock device is Bambu-bound.  dc_wifi migrates its Bambu credentials on
 * start; select that source once if the user has not made an explicit choice. */
static void select_migrated_source(void)
{
    nvs_handle_t nvs;
    uint8_t source;
    if (nvs_open("app_nvs", NVS_READONLY, &nvs) != ESP_OK) return;
    bool selected = nvs_get_u8(nvs, "ctl_src", &source) == ESP_OK;
    nvs_close(nvs);
    if (selected) return;

    dc_bambu_config_t bambu = {0};
    dc_bambu_get_config(&bambu);
    if (bambu.host[0]) {
        ESP_ERROR_CHECK(dc_source_set(DC_SRC_BAMBU));
        dc_evlog_add("migrated source -> Bambu");
    }
}

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    dc_evlog_console_init(); dc_evlog_init(); dc_evlog_add("DragonStatus boot");
    ESP_ERROR_CHECK(dc_wifi_set_identity(&(dc_wifi_identity_t){ .hostname = "dragonstatus", .instance_name = "DragonStatus", .ap_ssid_prefix = "DragonStatus_", .ap_password = DC_WIFI_DEFAULT_AP_PASSWORD }));
    ESP_ERROR_CHECK(dc_wifi_start());
    select_migrated_source();
    switch (dc_source_get()) {
    case DC_SRC_BAMBU: ESP_ERROR_CHECK(dc_bambu_start()); break;
    case DC_SRC_KLIPPER: ESP_ERROR_CHECK(dc_moonraker_start()); break;
    default: break;
    }
    ESP_ERROR_CHECK(ds_lighting_start());
    ESP_ERROR_CHECK(ds_audio_start());
    ESP_ERROR_CHECK(ds_portal_start());
    for (;;) { update_lighting(); vTaskDelay(pdMS_TO_TICKS(500)); }
}
