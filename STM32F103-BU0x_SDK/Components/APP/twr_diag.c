#include <stdint.h>
#include "uwb_pdoa.h"

volatile uint32_t twr_tag_timeout_count = 0;
volatile uint32_t twr_node_error_count = 0;
volatile uint32_t twr_tag_tx_count = 0;
volatile uint32_t twr_tag_rx_count = 0;
volatile uint32_t twr_node_tx_count = 0;
volatile uint32_t twr_node_rx_count = 0;

extern void __real_twr_rx_tag_timeout_cb(void);
extern void __real_twr_rx_error_cb(void);
extern void __real_twr_tx_tag_cb(void);
extern void __real_twr_rx_tag_cb(void);
extern void __real_twr_tx_node_cb(void);
extern void __real_twr_rx_node_cb(void);

void __wrap_twr_rx_tag_timeout_cb(void)
{
    twr_tag_timeout_count++;
    __real_twr_rx_tag_timeout_cb();
}

void __wrap_twr_rx_error_cb(void)
{
    twr_node_error_count++;
    __real_twr_rx_error_cb();
}

void __wrap_twr_tx_tag_cb(void)
{
    twr_tag_tx_count++;
    __real_twr_tx_tag_cb();
}

void __wrap_twr_rx_tag_cb(void)
{
    twr_tag_rx_count++;
    __real_twr_rx_tag_cb();
}

void __wrap_twr_tx_node_cb(void)
{
    twr_node_tx_count++;
    __real_twr_tx_node_cb();
}

void __wrap_twr_rx_node_cb(void)
{
    twr_node_rx_count++;
    __real_twr_rx_node_cb();
}
