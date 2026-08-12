#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "dc_lighting.h"

/* Exact output GPIO and physical pixel count need hardware/Ghidra confirmation.
 * Returning false is intentional: an unverified pin must never energize a strip. */
bool ds_board_lighting_outputs(dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS], uint8_t *count);
