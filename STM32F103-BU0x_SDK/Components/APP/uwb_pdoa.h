#ifndef __UWB_PDOA_H__
#define __UWB_PDOA_H__

#include "default_config.h"

#ifdef __cplusplus
extern "C" {
#endif

extern volatile int16_t uwb_range_pdoa_raw;
extern volatile int16_t uwb_poll_pdoa_raw;
extern volatile int16_t uwb_final_pdoa;
extern volatile int16_t uwb_last_rx_pdoa_raw;
extern uint8_t uwb_pdoa_lna_enable;

void uwb_pdoa_prepare_config(param_block_t *pcfg);
void uwb_pdoa_apply_after_start(uint8_t is_anchor);
void uwb_pdoa_latch_on_node_rx(void);
int16_t uwb_pdoa_raw_to_deg_x10(int16_t raw);
void uwb_pdoa_polar_to_xy_cm(int dist_cm, int angle_deg_x10, int *x_cm, int *y_cm);

#ifdef __cplusplus
}
#endif

#endif
