#include "ds_board.h"

bool ds_board_lighting_outputs(dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS], uint8_t *count)
{
    if (!outputs || !count) return false;

    /* The default target stays visibly testable on the development C3. */
#if CONFIG_DS_BOARD_STATUS_PRODUCTION
    /* OEM app_rgb.c initializes RMT TX with gpio_num=4 and submits its GRB
     * framebuffer as nine bytes, i.e. three WS2812 pixels. */
    outputs[0] = (dc_lighting_output_t){
        .gpio = GPIO_NUM_4,
        .pixels = 3,
        .reverse = false,
    };
#else
    outputs[0] = (dc_lighting_output_t){
        .gpio = GPIO_NUM_8,
        .pixels = 1,
        .reverse = false,
    };
#endif
    *count = 1;
    return true;
}
