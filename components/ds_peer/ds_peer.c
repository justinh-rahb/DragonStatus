#include "ds_peer.h"

#include "dc_lighting.h"
#include "dc_peer.h"
#include "ds_board.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define DS_PEER_HEARTBEAT_MS 5000

static const char *TAG = "ds_peer";
static char s_peer_id[DC_PEER_ID_MAX];
static dc_peer_lighting_t s_state;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;

static dc_peer_printer_t peer_printer_state(ds_printer_state_t state)
{
    switch (state) {
    case DS_PRINTER_IDLE:
    case DS_PRINTER_COMPLETE:    return DC_PEER_PRINTER_IDLE;
    case DS_PRINTER_DOWNLOADING:
    case DS_PRINTER_PREPARING:
    case DS_PRINTER_PRINTING:    return DC_PEER_PRINTER_PRINTING;
    case DS_PRINTER_PAUSED:      return DC_PEER_PRINTER_PAUSED;
    case DS_PRINTER_ERROR:       return DC_PEER_PRINTER_ERROR;
    default:                     return DC_PEER_PRINTER_OFFLINE;
    }
}

static void current_ip(uint8_t out[4])
{
    memset(out, 0, 4);
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info = {0};
    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK) return;
    out[0] = esp_ip4_addr1(&info.ip);
    out[1] = esp_ip4_addr2(&info.ip);
    out[2] = esp_ip4_addr3(&info.ip);
    out[3] = esp_ip4_addr4(&info.ip);
}

static void publish_announce(void)
{
    dc_peer_announce_t announce = {
        .kind = DC_PEER_KIND_STATUS,
        .caps = DC_PEER_CAP_BIT(DC_PEER_CAP_ANNOUNCE) |
                DC_PEER_CAP_BIT(DC_PEER_CAP_LIGHTING),
    };
    current_ip(announce.ip);
    snprintf(announce.name, sizeof(announce.name), "DragonStatus");
    snprintf(announce.fw, sizeof(announce.fw), "%.*s",
             (int)sizeof(announce.fw) - 1, esp_app_get_description()->version);
    (void)dc_peer_publish(DC_PEER_CAP_ANNOUNCE, &announce, sizeof(announce));
}

static void publish_task(void *arg)
{
    (void)arg;
    for (;;) {
        dc_peer_lighting_t state;
        portENTER_CRITICAL(&s_state_mux);
        state = s_state;
        portEXIT_CRITICAL(&s_state_mux);
        publish_announce();
        (void)dc_peer_publish(DC_PEER_CAP_LIGHTING, &state, sizeof(state));
        vTaskDelay(pdMS_TO_TICKS(DS_PEER_HEARTBEAT_MS));
    }
}

esp_err_t ds_peer_start(void)
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(s_peer_id, sizeof(s_peer_id), "dragonstatus-%02x%02x", mac[4], mac[5]);
    err = dc_peer_start(s_peer_id);
    if (err != ESP_OK) return err;
    if (xTaskCreate(publish_task, "ds_peer", 3072, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "publishing as %s", s_peer_id);
    return ESP_OK;
}

void ds_peer_update(ds_printer_state_t state, float progress)
{
    ds_lighting_config_t config;
    ds_lighting_stats_t policy;
    dc_lighting_stats_t renderer;
    ds_lighting_get_config(&config);
    ds_lighting_get_stats(&policy);
    dc_lighting_get_stats(&renderer);

    dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS];
    uint8_t output_count = 0;
    bool hardware_ready = ds_board_lighting_outputs(outputs, &output_count);
    uint16_t wire_pixels = 0;
    for (uint8_t i = 0; i < output_count; ++i) wire_pixels += outputs[i].pixels;

    uint8_t progress_pct = 0;
    if (isfinite(progress) && progress >= 0.0f) {
        if (progress > 1.0f) progress = 1.0f;
        progress_pct = (uint8_t)lroundf(progress * 100.0f);
    }
    dc_peer_lighting_t next = {
        .printer_state = (uint8_t)peer_printer_state(state),
        .progress_pct = progress_pct,
        .brightness = config.brightness,
        .effect = policy.last_effect,
        .flags = (config.enabled ? DC_PEER_LIGHTING_ENABLED : 0) |
                 (hardware_ready ? DC_PEER_LIGHTING_HARDWARE_READY : 0) |
                 (renderer.failing ? DC_PEER_LIGHTING_RENDERER_FAULT : 0) |
                 (policy.standby_blanking ? DC_PEER_LIGHTING_STANDBY_BLANK : 0),
        .color = {policy.last_color[0], policy.last_color[1], policy.last_color[2]},
        .wire_pixels = wire_pixels,
    };

    portENTER_CRITICAL(&s_state_mux);
    next.state_revision = s_state.state_revision;
    if (memcmp(&next, &s_state, sizeof(next)) != 0) {
        next.state_revision++;
        s_state = next;
    }
    portEXIT_CRITICAL(&s_state_mux);
}
