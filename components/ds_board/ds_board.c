#include "ds_board.h"

bool ds_board_lighting_outputs(dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS], uint8_t *count)
{
    if (!outputs || !count) return false;

    /* Development C3 preview target: the on-board WS2812 is on GPIO8.  The
     * production Panda Status mapping is recovered separately from the OEM
     * image; do not interpret this single-pixel development output as that
     * board's final strip configuration. */
    outputs[0] = (dc_lighting_output_t){
        .gpio = GPIO_NUM_8,
        .pixels = 1,
        .reverse = false,
    };
    *count = 1;
    return true;
}
