#include "ds_lighting.h"
#include "ds_board.h"
#include "dc_lighting.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include <stddef.h>
#include <string.h>

#define DS_LIGHT_NVS_NS "dragonstatus"
#define DS_LIGHT_LEGACY_NVS_NS "app_nvs"
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
    /* The remaining factory H2D palette, previously inline in the policy. */
    .mode = DS_LIGHT_MODE_PRINTER,
    .fixed_color = {255, 255, 255},
    .unbound_color = {0, 80, 255},
    .downloading_color = {255, 221, 0},
    .preparing_color = {248, 163, 35},
    .paused_color = {255, 255, 255},
    .complete_color = {0, 255, 42},
    .complete_hold_min = 15,
    .standby_min = 120,
};

void ds_lighting_get_config(ds_lighting_config_t *out) { if (out) *out = s_config; }

static esp_err_t store_config(void)
{
    nvs_handle_t nvs;
    if (nvs_open(DS_LIGHT_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return ESP_FAIL;
    esp_err_t err = nvs_set_blob(nvs, DS_LIGHT_NVS_KEY, &s_config, sizeof(s_config));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static const char *TAG = "ds_lighting";
static int64_t s_complete_since_us;
static int64_t s_idle_since_us;
static uint8_t s_output_count;

/* Brightness and strip direction live in the renderer, so a saved change has to
 * be pushed there as well as persisted; otherwise it only appears after a
 * reboot. */
static void apply_runtime_config(void)
{
    (void)dc_lighting_set_brightness(s_config.brightness);
    for (uint8_t i = 0; i < s_output_count; ++i) (void)dc_lighting_set_output_reverse(i, s_config.reverse);
}

esp_err_t ds_lighting_set_config(const ds_lighting_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    apply_runtime_config();
    return store_config();
}

static dc_lighting_effect_t renderer_effect(uint8_t effect)
{
    switch ((ds_lighting_effect_t)effect) {
    case DS_LIGHTING_SOLID: return DC_LIGHTING_SOLID;
    case DS_LIGHTING_BREATHE: return DC_LIGHTING_BREATHE;
    case DS_LIGHTING_RAINBOW: return DC_LIGHTING_RAINBOW;
    case DS_LIGHTING_BLINK: return DC_LIGHTING_STROBE;
    case DS_LIGHTING_FLOW: return DC_LIGHTING_FLOW;
    case DS_LIGHTING_CYLON: return DC_LIGHTING_CYLON;
    case DS_LIGHTING_MUSIC: return DC_LIGHTING_AUDIO_METER;
    case DS_LIGHTING_CYCLE: return DC_LIGHTING_CYCLE;
    case DS_LIGHTING_WAVE: return DC_LIGHTING_WAVE;
    case DS_LIGHTING_MARQUEE: return DC_LIGHTING_MARQUEE;
    default: return DC_LIGHTING_SOLID;
    }
}

/* Tolerant load: a shorter stored blob (an older config layout) is copied into
 * the front of s_config, leaving any newer fields at their compiled defaults.
 * Returns the number of stored bytes, or zero when the key is absent. */
static size_t load_blob(const char *namespace)
{
    nvs_handle_t nvs;
    if (nvs_open(namespace, NVS_READONLY, &nvs) != ESP_OK) return 0;
    size_t need = 0, got = 0;
    if (nvs_get_blob(nvs, DS_LIGHT_NVS_KEY, NULL, &need) == ESP_OK && need && need <= sizeof(s_config)) {
        uint8_t blob[sizeof(s_config)];
        got = sizeof(blob);
        if (nvs_get_blob(nvs, DS_LIGHT_NVS_KEY, blob, &got) == ESP_OK) memcpy(&s_config, blob, got);
        else got = 0;
    }
    nvs_close(nvs);
    return got;
}

esp_err_t ds_lighting_start(void)
{
    size_t loaded = load_blob(DS_LIGHT_NVS_NS);
    bool migrated = false;
    if (!loaded) {
        /* Earlier Status builds stored the product lighting blob beside Core
         * source/Wi-Fi data in app_nvs. Copy it once, never erase it, and keep
         * future Status-only settings in DragonStatus's unique namespace. */
        loaded = load_blob(DS_LIGHT_LEGACY_NVS_NS);
        migrated = loaded != 0;
    }
    if (loaded && loaded <= offsetof(ds_lighting_config_t, mode)) {
        /* Before the palette gained a mode, an effect override always painted
         * the printing colour. Carry that forward as the fixed colour so the
         * upgrade is invisible to anyone already running an override. */
        memcpy(s_config.fixed_color, s_config.printing_color, sizeof(s_config.fixed_color));
        migrated = true;
    }
    /* Historic Status UI values were product choices, but were passed straight
     * to the renderer. Keeping those IDs as policy IDs fixes their offset. */
    if (s_config.effect > DS_LIGHTING_MARQUEE) s_config.effect = DS_LIGHTING_FACTORY_H2D;
    if (s_config.mode > DS_LIGHT_MODE_FIXED) s_config.mode = DS_LIGHT_MODE_PRINTER;
    if (migrated) (void)store_config();
    dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS]; uint8_t count = 0;
    if (!ds_board_lighting_outputs(outputs, &count)) {
        ESP_LOGW(TAG, "RGB output disabled until board pinout is verified");
        return ESP_OK;
    }
    for (uint8_t i = 0; i < count; ++i) outputs[i].reverse = s_config.reverse;
    esp_err_t err = dc_lighting_start(&(dc_lighting_config_t){ .outputs = outputs, .output_count = count, .brightness = s_config.brightness, .fps = 30 });
    if (err == ESP_OK) s_output_count = count;
    return err;
}

void ds_lighting_update(ds_printer_state_t state, float progress)
{
    if (!s_config.enabled) { (void)dc_lighting_off(); return; }
    int64_t now = esp_timer_get_time();
    /* OEM finish indication holds the completion colour, then returns to the
     * normal idle breathing colour if the printer has not sent IDLE yet. */
    if (state == DS_PRINTER_COMPLETE) {
        if (!s_complete_since_us) s_complete_since_us = now;
        if (s_config.complete_hold_min &&
            now - s_complete_since_us >= (int64_t)s_config.complete_hold_min * 60LL * 1000000LL) state = DS_PRINTER_IDLE;
    } else {
        s_complete_since_us = 0;
    }
    /* Documented OEM 1.0.1 standby. Music is a meter rather than a status
     * light, so it keeps running while the printer sits idle. */
    bool resting = state == DS_PRINTER_IDLE || state == DS_PRINTER_UNKNOWN;
    if (!resting) s_idle_since_us = 0;
    else if (!s_idle_since_us) s_idle_since_us = now;
    if (resting && s_config.standby_min && s_config.effect != DS_LIGHTING_MUSIC &&
        now - s_idle_since_us >= (int64_t)s_config.standby_min * 60LL * 1000000LL) {
        (void)dc_lighting_off();
        return;
    }

    const uint8_t *palette = s_config.idle_color;
    dc_lighting_effect_t fx = DC_LIGHTING_SOLID;
    switch (state) {
    case DS_PRINTER_UNKNOWN:     palette = s_config.unbound_color; fx = DC_LIGHTING_FLOW; break;
    case DS_PRINTER_DOWNLOADING: palette = s_config.downloading_color; fx = DC_LIGHTING_FLOW; break;
    case DS_PRINTER_PREPARING:   palette = s_config.preparing_color; fx = DC_LIGHTING_FLOW; break;
    case DS_PRINTER_PRINTING:    palette = s_config.printing_color; fx = progress >= 0.0f ? DC_LIGHTING_PROGRESS : DC_LIGHTING_SOLID; break;
    case DS_PRINTER_PAUSED:      palette = s_config.paused_color; fx = DC_LIGHTING_BREATHE; break;
    case DS_PRINTER_COMPLETE:    palette = s_config.complete_color; break;
    case DS_PRINTER_ERROR:       palette = s_config.error_color; fx = DC_LIGHTING_BLINK; break;
    case DS_PRINTER_IDLE:        palette = s_config.idle_color; fx = DC_LIGHTING_BREATHE; break;
    default: break;
    }
    /* A fixed colour lets the effect alone carry the state, but a fault has to
     * stay recognisable, so an error keeps its own colour either way. */
    if (s_config.mode == DS_LIGHT_MODE_FIXED && state != DS_PRINTER_ERROR) palette = s_config.fixed_color;
    dc_rgb_t color = {palette[0], palette[1], palette[2]};
    (void)dc_lighting_set_progress(progress);
    if (s_config.effect != DS_LIGHTING_FACTORY_H2D) fx = renderer_effect(s_config.effect);
    (void)dc_lighting_set(color, fx, s_config.speed);
}

void ds_lighting_set_audio_level(float level)
{
    (void)dc_lighting_set_audio_level(level);
}
