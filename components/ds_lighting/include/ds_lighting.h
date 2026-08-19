#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    DS_PRINTER_UNKNOWN, DS_PRINTER_IDLE, DS_PRINTER_DOWNLOADING, DS_PRINTER_PREPARING, DS_PRINTER_PRINTING,
    DS_PRINTER_PAUSED, DS_PRINTER_COMPLETE, DS_PRINTER_ERROR,
} ds_printer_state_t;

/* Product-facing IDs deliberately do not mirror dc_lighting_effect_t: zero
 * means "follow factory H2D policy", while Core's zero is Solid. */
typedef enum {
    DS_LIGHTING_FACTORY_H2D = 0,
    DS_LIGHTING_SOLID = 1,
    DS_LIGHTING_BREATHE = 2,
    DS_LIGHTING_RAINBOW = 3,
    DS_LIGHTING_BLINK = 4,
    DS_LIGHTING_FLOW = 5,
    DS_LIGHTING_CYLON = 6,
    DS_LIGHTING_MUSIC = 7,
    DS_LIGHTING_CYCLE = 8,
    DS_LIGHTING_WAVE = 9,
    DS_LIGHTING_MARQUEE = 10,
} ds_lighting_effect_t;

/* What the colour is taken from.  The effect field stays independent: a mode
 * chooses the colour, an effect chooses the animation. */
typedef enum {
    DS_LIGHT_MODE_PRINTER = 0,  /* per-state palette below */
    DS_LIGHT_MODE_FIXED = 1,    /* one colour for every state */
} ds_lighting_mode_t;

/* Stable product-side subset of DragonVent's lighting resource.  The generic
 * renderer owns animation; Status owns these OEM-policy defaults.
 *
 * NOTE: append new fields at the END — the loader tolerates a shorter stored
 * blob (older installs keep their values; new fields take these defaults). */
typedef struct {
    bool enabled;
    uint8_t brightness;
    uint8_t speed;
    /* 0 retains OEM H2D effect policy; nonzero is a ds_lighting_effect_t
     * override applied to every state. */
    uint8_t effect;
    /* OEM H2D exposes three semantic palette entries; the rest of the factory
     * palette is documented policy and configurable in the tail below. */
    uint8_t idle_color[3];
    uint8_t printing_color[3];
    uint8_t error_color[3];
    uint8_t mode;                  /* ds_lighting_mode_t */
    uint8_t fixed_color[3];        /* mode == DS_LIGHT_MODE_FIXED */
    uint8_t unbound_color[3];      /* no printer bound / state unknown */
    uint8_t downloading_color[3];
    uint8_t preparing_color[3];
    uint8_t paused_color[3];
    uint8_t complete_color[3];
    bool reverse;                  /* reverse the strip for spatial effects */
    /* Documented OEM 1.0.1 policy, kept configurable rather than hard-coded. */
    uint8_t complete_hold_min;     /* 0 holds until the printer reports idle */
    uint8_t standby_min;           /* idle blackout; 0 never sleeps */
} ds_lighting_config_t;

/* Policy health. The renderer can be perfectly alive while the policy either
 * stops being driven or deliberately blanks the strip; from outside both look
 * like a dark bar with a healthy config. These expose which, and how long the
 * two OEM timers have been running. */
typedef struct {
    uint32_t updates;          /* ds_lighting_update() calls since boot */
    bool standby_blanking;     /* the last update blanked the strip for standby */
    bool disabled_blanking;    /* ...or because lighting is switched off */
    uint32_t resting_s;        /* seconds in the current idle window, 0 if awake */
    uint32_t complete_s;       /* seconds since the completion hold began */
    uint8_t last_state;        /* ds_printer_state_t after hold/standby policy */
    uint8_t last_effect;       /* dc_lighting_effect_t handed to the renderer */
    uint8_t last_color[3];
} ds_lighting_stats_t;

void ds_lighting_get_stats(ds_lighting_stats_t *out);

esp_err_t ds_lighting_start(void);
void ds_lighting_update(ds_printer_state_t state, float progress);
/* Board audio adapter supplies its filtered 0..1 level here. */
void ds_lighting_set_audio_level(float level);
void ds_lighting_get_config(ds_lighting_config_t *out);
esp_err_t ds_lighting_set_config(const ds_lighting_config_t *config);
