#include <stdio.h>
#include <string.h>

#include "uwb_data.h"
#include "Generic_cmd.h"
#include "config.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "uwb.h"
#include "OSAL_Comdef.h"

extern volatile uint8_t uwb_ranging_active;

extern void disable_deca_irq(void);
extern void enable_deca_irq(void);

typedef struct
{
    uint8_t  valid;
    uint16_t dest;
    uint16_t len;
    uint8_t  payload[UWB_DATA_MAX_PAYLOAD];
} uwb_data_pkt_t;

static uwb_data_pkt_t uwb_tx_queue;
static uwb_data_pkt_t uwb_rx_pending;

static struct
{
    uint8_t  active;
    uint16_t dest;
    uint16_t len;
    uint16_t got;
    uint8_t  buf[UWB_DATA_MAX_PAYLOAD];
} uwb_uart_upload;

static uint8_t uwb_data_seq;
static uint32_t uwb_data_last_tx_tick;

static uint16_t uwb_data_pan_id(void)
{
    if (app.pConfig != NULL && app.pConfig->s.s_pdoa.panID != 0U)
    {
        return app.pConfig->s.s_pdoa.panID;
    }
    if (app.pConfig != NULL)
    {
        return app.pConfig->s.userConfig.nodePanid;
    }
    return 0x1111U;
}

static uint16_t uwb_data_src_addr(void)
{
    if (app.pConfig != NULL)
    {
        return app.pConfig->s.userConfig.nodeAddr;
    }
    return 0U;
}

static int uwb_data_hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}

int uwb_data_hex_decode(const char *hex, uint8_t *out, uint16_t len)
{
    uint16_t i;

    if (hex == NULL || out == NULL)
    {
        return -1;
    }

    for (i = 0U; i < len; i++)
    {
        int hi = uwb_data_hex_nibble(hex[i * 2U]);
        int lo = uwb_data_hex_nibble(hex[i * 2U + 1U]);

        if (hi < 0 || lo < 0)
        {
            return -1;
        }
        out[i] = (uint8_t) ((hi << 4) | lo);
    }

    return 0;
}

void uwb_data_init(void)
{
    uwb_tx_queue.valid = 0U;
    uwb_rx_pending.valid = 0U;
    uwb_uart_upload.active = 0U;
    uwb_data_seq = 0U;
    uwb_data_last_tx_tick = 0U;
}

int uwb_data_queue_tx(uint16_t dest, const uint8_t *payload, uint16_t len)
{
    if (payload == NULL || len == 0U || len > UWB_DATA_MAX_PAYLOAD)
    {
        return -1;
    }
    if (uwb_tx_queue.valid)
    {
        return -1;
    }

    uwb_tx_queue.dest = dest;
    uwb_tx_queue.len = len;
    memcpy(uwb_tx_queue.payload, payload, len);
    uwb_tx_queue.valid = 1U;
    return 0;
}

int uwb_data_uart_begin(uint16_t dest, uint16_t len)
{
    if (len == 0U || len > UWB_DATA_MAX_PAYLOAD)
    {
        return -1;
    }
    if (uwb_uart_upload.active)
    {
        return -1;
    }

    uwb_uart_upload.active = 1U;
    uwb_uart_upload.dest = dest;
    uwb_uart_upload.len = len;
    uwb_uart_upload.got = 0U;
    return 0;
}

void uwb_data_uart_abort(void)
{
    uwb_uart_upload.active = 0U;
    uwb_uart_upload.got = 0U;
}

int uwb_data_uart_active(void)
{
    return uwb_uart_upload.active ? 1 : 0;
}

static void uwb_data_uart_finish_ok(void)
{
    port_tx_msg((uint8_t *) "\r\nOK\r\n", 6);
}

static void uwb_data_uart_finish_err(void)
{
    port_tx_msg((uint8_t *) "\r\nERR\r\n", 7);
}

int uwb_data_uart_feed(uint8_t byte)
{
    if (!uwb_uart_upload.active)
    {
        return 0;
    }

    if (uwb_uart_upload.got >= uwb_uart_upload.len)
    {
        return 0;
    }

    uwb_uart_upload.buf[uwb_uart_upload.got++] = byte;
    if (uwb_uart_upload.got < uwb_uart_upload.len)
    {
        return 1;
    }

    uwb_uart_upload.active = 0U;
    if (uwb_data_queue_tx(uwb_uart_upload.dest, uwb_uart_upload.buf, uwb_uart_upload.len) != 0)
    {
        uwb_data_uart_finish_err();
        return 1;
    }

    uwb_data_uart_finish_ok();
    return 1;
}

static uint16_t uwb_data_build_frame(uint8_t *frame, uint16_t dest, const uint8_t *payload, uint16_t len)
{
    uint16_t pan = uwb_data_pan_id();
    uint16_t src = uwb_data_src_addr();
    uint16_t total = (uint16_t) (UWB_DATA_HDR_LEN + len + 2U);

    frame[0] = 0x41U;
    frame[1] = 0x88U;
    frame[2] = uwb_data_seq++;
    frame[3] = (uint8_t) (pan & 0xFFU);
    frame[4] = (uint8_t) (pan >> 8);
    frame[5] = (uint8_t) (dest & 0xFFU);
    frame[6] = (uint8_t) (dest >> 8);
    frame[7] = (uint8_t) (src & 0xFFU);
    frame[8] = (uint8_t) (src >> 8);
    frame[9] = 0x00U;
    frame[10] = UWB_DATA_MSG_TYPE;
    memcpy(frame + UWB_DATA_HDR_LEN, payload, len);
    return total;
}

static int uwb_data_send_frame(const uint8_t *frame, uint16_t total_len)
{
    uint32_t deadline;
    uint32_t st;

    disable_deca_irq();
    dwt_forcetrxoff();
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_TX);

    if (dwt_writetxdata((uint16_t) (total_len - 2U), (uint8_t *) frame, 0) != DWT_SUCCESS)
    {
        enable_deca_irq();
        return -1;
    }
    dwt_writetxfctrl(total_len, 0, 0);
    if (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS)
    {
        enable_deca_irq();
        return -1;
    }

    deadline = time32_incr + 20U;
    while (time32_incr < deadline)
    {
        st = dwt_read32bitreg(SYS_STATUS_ID);
        if (st & SYS_STATUS_TXFRS_BIT_MASK)
        {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
            enable_deca_irq();
            return 0;
        }
    }

    dwt_forcetrxoff();
    enable_deca_irq();
    return -1;
}

static void uwb_data_emit_rx_uart(void)
{
    char hdr[48];
    int n;

    if (!uwb_rx_pending.valid)
    {
        return;
    }

    n = snprintf(hdr, sizeof(hdr), "\r\n+UWBDATA:%u,%u\r\n",
                 (unsigned) uwb_rx_pending.dest,
                 (unsigned) uwb_rx_pending.len);
    if (n > 0)
    {
        port_tx_msg((uint8_t *) hdr, (uint16_t) n);
    }
    if (uwb_rx_pending.len > 0U)
    {
        port_tx_msg(uwb_rx_pending.payload, uwb_rx_pending.len);
    }
    port_tx_msg((uint8_t *) "\r\n", 2);
    uwb_rx_pending.valid = 0U;
}

void uwb_data_on_rx_frame(uint16_t frame_len)
{
    uint8_t hdr[UWB_DATA_HDR_LEN];
    uint16_t payload_len;
    uint16_t src;

    if (frame_len <= (UWB_DATA_HDR_LEN + 2U) || frame_len > 1023U)
    {
        return;
    }
    if (uwb_rx_pending.valid)
    {
        return;
    }

    dwt_readrxdata(hdr, UWB_DATA_HDR_LEN, 0);
    if (hdr[10] != UWB_DATA_MSG_TYPE)
    {
        return;
    }

    payload_len = (uint16_t) (frame_len - UWB_DATA_HDR_LEN - 2U);
    if (payload_len == 0U || payload_len > UWB_DATA_MAX_PAYLOAD)
    {
        return;
    }

    src = (uint16_t) hdr[7] | ((uint16_t) hdr[8] << 8);
    uwb_rx_pending.dest = src;
    uwb_rx_pending.len = payload_len;
    dwt_readrxdata(uwb_rx_pending.payload, payload_len, UWB_DATA_HDR_LEN);
    uwb_rx_pending.valid = 1U;
}

void uwb_data_tick(void)
{
    static uint16_t deliver_div = 0;
    uint8_t frame[UWB_DATA_HDR_LEN + UWB_DATA_MAX_PAYLOAD];

    if (uwb_rx_pending.valid)
    {
        deliver_div++;
        if (deliver_div >= 200U)
        {
            deliver_div = 0U;
            uwb_data_emit_rx_uart();
        }
    }

    if (!uwb_tx_queue.valid || !uwb_ranging_active)
    {
        return;
    }

    if ((time32_incr - uwb_data_last_tx_tick) < 500U)
    {
        return;
    }

    {
        uint16_t total = uwb_data_build_frame(frame, uwb_tx_queue.dest,
                                              uwb_tx_queue.payload, uwb_tx_queue.len);
        if (uwb_data_send_frame(frame, total) == 0)
        {
            uwb_tx_queue.valid = 0U;
            uwb_data_last_tx_tick = time32_incr;
        }
    }
}
