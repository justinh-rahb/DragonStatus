#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "dc_lighting.h"

/* GPIO4 and the Status-family WS2812 protocol are OEM-confirmed. Production
 * renders the 27-pixel PopStatus superset; shorter 25-pixel Panda chains
 * discard the two trailing records. */
bool ds_board_lighting_outputs(dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS], uint8_t *count);
