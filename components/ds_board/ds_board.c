#include "ds_board.h"

bool ds_board_lighting_outputs(dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS], uint8_t *count)
{
    (void)outputs;
    if (count) *count = 0;
    return false;
}
