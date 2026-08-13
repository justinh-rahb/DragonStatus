#include "ds_portal.h"

#include "cJSON.h"
#include "dc_bambu.h"
#include "dc_moonraker.h"
#include "dc_portal.h"
#include "dc_source.h"
#include "dc_wifi.h"
#include "ds_board.h"
#include "ds_lighting.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_server.h"

#include <stdlib.h>
#include <string.h>

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return err;
}

static cJSON *recv_json(httpd_req_t *req)
{
    if (!req->content_len || req->content_len > 1024) return NULL;
    char body[1025]; int got = httpd_req_recv(req, body, req->content_len);
    if (got != req->content_len) return NULL;
    body[got] = '\0'; return cJSON_Parse(body);
}

static cJSON *lighting_json(void)
{
    ds_lighting_config_t config; ds_lighting_get_config(&config);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "api_version", 2);
    cJSON_AddBoolToObject(root, "enabled", config.enabled);
    cJSON_AddNumberToObject(root, "brightness", config.brightness);
    cJSON_AddNumberToObject(root, "speed", config.speed);
    cJSON_AddNumberToObject(root, "effect", config.effect);
    cJSON_AddStringToObject(root, "profile", "factory_h2d");
    return root;
}

static esp_err_t lighting_get(httpd_req_t *req) { return send_json(req, lighting_json()); }

static esp_err_t lighting_post(httpd_req_t *req)
{
    cJSON *body = recv_json(req); if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
    ds_lighting_config_t config; ds_lighting_get_config(&config); cJSON *value;
    if ((value = cJSON_GetObjectItemCaseSensitive(body, "enabled")) && cJSON_IsBool(value)) config.enabled = cJSON_IsTrue(value);
    if ((value = cJSON_GetObjectItemCaseSensitive(body, "brightness")) && cJSON_IsNumber(value)) config.brightness = (uint8_t)(value->valueint < 0 ? 0 : value->valueint > 255 ? 255 : value->valueint);
    if ((value = cJSON_GetObjectItemCaseSensitive(body, "speed")) && cJSON_IsNumber(value)) config.speed = (uint8_t)(value->valueint < 0 ? 0 : value->valueint > 255 ? 255 : value->valueint);
    if ((value = cJSON_GetObjectItemCaseSensitive(body, "effect")) && cJSON_IsNumber(value)) config.effect = (uint8_t)(value->valueint < 0 ? 0 : value->valueint > DC_LIGHTING_PROGRESS ? DC_LIGHTING_PROGRESS : value->valueint);
    cJSON_Delete(body); if (ds_lighting_set_config(&config) != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save lighting");
    return send_json(req, lighting_json());
}

static cJSON *setup_field(cJSON *fields, const char *key, const char *label,
                          const char *type, const char *value)
{
    cJSON *field = cJSON_CreateObject();
    cJSON_AddStringToObject(field, "key", key);
    cJSON_AddStringToObject(field, "label", label);
    cJSON_AddStringToObject(field, "type", type);
    cJSON_AddStringToObject(field, "value", value ?: "");
    cJSON_AddItemToArray(fields, field);
    return field;
}

static void visible_when(cJSON *section, const char *key, const char *value)
{
    cJSON *when = cJSON_AddObjectToObject(section, "visible_when");
    cJSON_AddStringToObject(when, "field", key);
    cJSON_AddStringToObject(when, "value", value);
}

static const char *source_value(dc_ctl_source_t source)
{
    switch (source) {
    case DC_SRC_BAMBU: return "bambu";
    case DC_SRC_KLIPPER: return "klipper";
    default: return "none";
    }
}

static cJSON *describe_product(void *ctx)
{
    (void)ctx;
    cJSON *root = cJSON_CreateObject();
    cJSON *sections = cJSON_AddArrayToObject(root, "sections");
    cJSON *printer = cJSON_CreateObject();
    cJSON_AddStringToObject(printer, "title", "Printer source");
    cJSON_AddStringToObject(printer, "description", "Choose one printer. A restart applies a source change.");
    cJSON *fields = cJSON_AddArrayToObject(printer, "fields");
    cJSON *source = setup_field(fields, "source", "Printer type", "select", source_value(dc_source_get()));
    cJSON *options = cJSON_AddArrayToObject(source, "options");
    const char *choices[][2] = {{"bambu", "Bambu LAN"}, {"klipper", "Klipper / Moonraker"}, {"none", "Standalone"}};
    for (size_t i = 0; i < sizeof(choices) / sizeof(choices[0]); ++i) {
        cJSON *option = cJSON_CreateObject();
        cJSON_AddStringToObject(option, "value", choices[i][0]);
        cJSON_AddStringToObject(option, "label", choices[i][1]);
        cJSON_AddItemToArray(options, option);
    }
    cJSON_AddItemToArray(sections, printer);

    dc_bambu_config_t bambu = {0}; dc_bambu_get_config(&bambu);
    cJSON *bambu_section = cJSON_CreateObject();
    cJSON_AddStringToObject(bambu_section, "title", "Bambu LAN");
    cJSON_AddStringToObject(bambu_section, "description", "Use the printer's LAN-mode access code. DragonStatus only reads printer state.");
    visible_when(bambu_section, "source", "bambu");
    fields = cJSON_AddArrayToObject(bambu_section, "fields");
    setup_field(fields, "bambu_host", "Printer address", "text", bambu.host);
    setup_field(fields, "bambu_serial", "Printer serial", "text", bambu.serial);
    cJSON_AddBoolToObject(setup_field(fields, "bambu_code", "LAN access code", "text", ""), "secret", true);
    cJSON_AddItemToArray(sections, bambu_section);

    dc_moonraker_config_t moonraker = {0}; dc_moonraker_get_config(&moonraker);
    char port[8]; snprintf(port, sizeof(port), "%u", moonraker.port ?: 7125);
    cJSON *moonraker_section = cJSON_CreateObject();
    cJSON_AddStringToObject(moonraker_section, "title", "Klipper / Moonraker");
    visible_when(moonraker_section, "source", "klipper");
    fields = cJSON_AddArrayToObject(moonraker_section, "fields");
    setup_field(fields, "moonraker_host", "Moonraker address", "text", moonraker.host);
    setup_field(fields, "moonraker_port", "Moonraker port", "number", port);
    cJSON_AddBoolToObject(setup_field(fields, "moonraker_api_key", "API key", "text", ""), "secret", true);
    cJSON_AddItemToArray(sections, moonraker_section);
    return root;
}

static const char *string_value(const cJSON *values, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(values, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static esp_err_t apply_product(const cJSON *values, void *ctx, char *message, size_t message_size)
{
    (void)ctx;
    const char *source_text = string_value(values, "source");
    if (source_text) {
        dc_ctl_source_t source = !strcmp(source_text, "bambu") ? DC_SRC_BAMBU :
                                  !strcmp(source_text, "klipper") ? DC_SRC_KLIPPER : DC_SRC_NONE;
        if (strcmp(source_text, "bambu") && strcmp(source_text, "klipper") && strcmp(source_text, "none")) {
            snprintf(message, message_size, "Unknown printer source"); return ESP_ERR_INVALID_ARG;
        }
        ESP_RETURN_ON_ERROR(dc_source_set(source), "ds_portal", "save source");
    }
    const char *host = string_value(values, "bambu_host");
    if (host) {
        dc_bambu_config_t config = {0}; dc_bambu_get_config(&config);
        snprintf(config.host, sizeof(config.host), "%s", host);
        const char *serial = string_value(values, "bambu_serial"); if (serial) snprintf(config.serial, sizeof(config.serial), "%s", serial);
        const char *code = string_value(values, "bambu_code"); if (code && *code) snprintf(config.code, sizeof(config.code), "%s", code);
        ESP_RETURN_ON_ERROR(dc_bambu_set_config(&config), "ds_portal", "save Bambu");
    }
    host = string_value(values, "moonraker_host");
    if (host) {
        dc_moonraker_config_t config = {0}; dc_moonraker_get_config(&config);
        snprintf(config.host, sizeof(config.host), "%s", host);
        const char *port = string_value(values, "moonraker_port");
        if (port && *port) { long parsed = strtol(port, NULL, 10); if (parsed < 1 || parsed > 65535) { snprintf(message, message_size, "Invalid Moonraker port"); return ESP_ERR_INVALID_ARG; } config.port = (uint16_t)parsed; }
        const char *key = string_value(values, "moonraker_api_key"); if (key && *key) snprintf(config.api_key, sizeof(config.api_key), "%s", key);
        ESP_RETURN_ON_ERROR(dc_moonraker_set_config(&config), "ds_portal", "save Moonraker");
    }
    snprintf(message, message_size, "Printer settings saved. Restart to apply the selected source.");
    return ESP_OK;
}

static const char *wifi_state(dc_wifi_state_t state)
{
    switch (state) {
    case DC_WIFI_STATE_STA_CONNECTING: return "connecting";
    case DC_WIFI_STATE_STA_CONNECTED:  return "station";
    case DC_WIFI_STATE_AP_PORTAL:      return "setup_ap";
    default: return "starting";
    }
}

static const char *printer_state(void)
{
    if (dc_source_get() == DC_SRC_KLIPPER) {
        dc_moonraker_status_t status = {0};
        dc_moonraker_get_status(&status);
        return dc_printer_state_str(status.printer);
    }
    if (dc_source_get() == DC_SRC_BAMBU) {
        dc_bambu_status_t status = {0};
        dc_bambu_get_status(&status);
        return status.error ? "error" : status.printing ? "printing" : status.connected ? "idle" : "unknown";
    }
    return "standalone";
}

static esp_err_t info_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "api_version", 2);
    cJSON_AddStringToObject(root, "project", "dragonstatus");
    cJSON_AddStringToObject(root, "firmware", esp_app_get_description()->version);
    cJSON *caps = cJSON_AddArrayToObject(root, "capabilities");
    cJSON_AddItemToArray(caps, cJSON_CreateString("source_status"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("lighting"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("polling"));
    cJSON *ui = cJSON_AddObjectToObject(root, "ui");
    cJSON_AddNumberToObject(ui, "schema", 1);
    cJSON_AddStringToObject(ui, "product", "dragonstatus");
    cJSON_AddStringToObject(ui, "display_name", "DragonStatus");
    return send_json(req, root);
}

static esp_err_t state_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "api_version", 2);
    cJSON_AddStringToObject(root, "project", "dragonstatus");
    cJSON *printer = cJSON_AddObjectToObject(root, "printer");
    cJSON_AddStringToObject(printer, "source", dc_source_str(dc_source_get()));
    cJSON_AddStringToObject(printer, "state", printer_state());
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "state", wifi_state(dc_wifi_state()));
    cJSON *lighting = cJSON_AddObjectToObject(root, "lighting");
    dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS];
    uint8_t output_count = 0;
    cJSON_AddBoolToObject(lighting, "hardware_ready", ds_board_lighting_outputs(outputs, &output_count));
    cJSON_AddNumberToObject(lighting, "outputs", output_count);
    return send_json(req, root);
}

static const httpd_uri_t s_routes[] = {
    { .uri = "/api/v2/info", .method = HTTP_GET, .handler = info_get },
    { .uri = "/api/v2/state", .method = HTTP_GET, .handler = state_get },
    { .uri = "/api/v2/lighting", .method = HTTP_GET, .handler = lighting_get },
    { .uri = "/api/v2/lighting", .method = HTTP_POST, .handler = lighting_post },
};

esp_err_t ds_portal_start(void)
{
    return dc_portal_start(&(dc_portal_config_t){
        .product = "dragonstatus",
        .display_name = "DragonStatus",
        .product_routes = s_routes,
        .product_route_count = sizeof(s_routes) / sizeof(s_routes[0]),
        .describe_product = describe_product,
        .apply_product = apply_product,
    });
}
