#include "ds_board.h"

bool ds_board_lighting_outputs(dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS], uint8_t *count)
{
    if (!outputs || !count) return false;

    /* The default target stays visibly testable on the development C3. */
#if CONFIG_DS_BOARD_STATUS_PRODUCTION
    /* Every known Status-family enclosure uses the GPIO4 WS2812 data chain,
     * but Panda Status has 25 pixels and PopStatus has 27. WS2812 is a
     * one-way stream: a shorter chain safely discards trailing pixel records.
     * Emit the 27-pixel superset so one production image lights either bar. */
    outputs[0] = (dc_lighting_output_t){
        .gpio = GPIO_NUM_4,
        .pixels = 27,
        .reverse = false,
        .transport = DC_LIGHTING_TRANSPORT_RMT,
    };
#else
    outputs[0] = (dc_lighting_output_t){
        .gpio = GPIO_NUM_8,
        .pixels = 1,
        .reverse = false,
        .transport = DC_LIGHTING_TRANSPORT_RMT,
    };
#endif
    *count = 1;
    return true;
}
