#ifndef UWB_DATA_H
#define UWB_DATA_H

#include <stdint.h>

#define UWB_DATA_MSG_TYPE       0xD0U
#define UWB_DATA_HDR_LEN        11U
#define UWB_DATA_MAX_PAYLOAD    1010U

void uwb_data_init(void);
int uwb_data_queue_tx(uint16_t dest, const uint8_t *payload, uint16_t len);
int uwb_data_hex_decode(const char *hex, uint8_t *out, uint16_t len);

int uwb_data_uart_begin(uint16_t dest, uint16_t len);
int uwb_data_uart_feed(uint8_t byte);
void uwb_data_uart_abort(void);
int uwb_data_uart_active(void);

void uwb_data_on_rx_frame(uint16_t frame_len);
void uwb_data_tick(void);

#endif
