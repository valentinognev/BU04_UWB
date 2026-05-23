#include <stdint.h>
#include <string.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "deca_vals.h"
#include "uwb_pdoa.h"
#include "uwb_data.h"
#include "config.h"

extern uint8_t uwb_pdoa_lna_enable;

#define TWR_MSG_POLL        0x1aU
#define TWR_MSG_RESP        0x1bU
#define TWR_MSG_FINAL       0x1cU
#define TWR_POLL_FRAME_LEN  13U
#define TWR_FINAL_FRAME_LEN 110U
#define UWB_RX_HDR_BYTES    16U
#define MAC_OFF_PAN_LO      3U
#define MAC_OFF_MSG_TYPE    10U
#define MAC_OFF_SEQ         9U

volatile uint32_t dwt_status_txfrs_count = 0;
volatile uint32_t dwt_status_rxfcg_count = 0;
volatile uint32_t dwt_status_rxfr_count = 0;
volatile uint32_t dwt_status_rxto_count = 0;
volatile uint32_t dwt_status_rxerr_count = 0;
volatile uint32_t dwt_status_last = 0;

volatile uint8_t uwb_last_rx_len = 0;
volatile uint8_t uwb_last_rx_hdr[UWB_RX_HDR_BYTES];
volatile uint32_t uwb_rx_len13_count = 0;
volatile uint32_t uwb_rx_poll_match_count = 0;
volatile uint32_t uwb_rx_resp_match_count = 0;
volatile uint32_t uwb_rx_pan_match_count = 0;
volatile uint8_t uwb_last_rx_b8 = 0;
volatile uint8_t uwb_last_rx_b9 = 0;
volatile int16_t uwb_last_rx_pdoa_raw = 0;
volatile int16_t uwb_last_pdoa_pre = 0;
volatile int16_t uwb_last_pdoa_post = 0;
volatile int16_t uwb_last_pdoa_direct = 0;
volatile int16_t uwb_last_ipatov_poa = 0;
volatile int16_t uwb_last_sts_poa = 0;
volatile int16_t uwb_last_sts2_poa = 0;
volatile uint16_t uwb_last_sts_acc = 0;
volatile uint8_t uwb_last_pdoa_mode = 0;
volatile int16_t uwb_final_pdoa = 0;
volatile int16_t uwb_final_ipatov_poa = 0;
volatile int16_t uwb_final_sts_poa = 0;

extern void __real_process_deca_irq(void);
extern int __real_dwt_configure(dwt_config_t *config);
extern void *getTwrInfoPtr(void);

#define NODE_OFF_STATE   0
#define NODE_OFF_DONE    4
#define NODE_OFF_RX_FLAG 0x1ab

static int uwb_frame_is_final(uint16_t len, uint8_t msg_type)
{
    (void) msg_type;
    return (len == TWR_FINAL_FRAME_LEN);
}

static int16_t uwb_poa_to_signed(uint16_t poa)
{
    return (int16_t) poa;
}

static void uwb_snapshot_rx_meta(uint16_t len, const uint8_t *snap, uint8_t n)
{
    uint8_t i;

    uwb_last_rx_len = (uint8_t) len;
    uwb_last_rx_b8 = (n > MAC_OFF_MSG_TYPE) ? snap[MAC_OFF_MSG_TYPE] : 0U;
    uwb_last_rx_b9 = (n > MAC_OFF_SEQ) ? snap[MAC_OFF_SEQ] : 0U;
    for (i = 0U; i < UWB_RX_HDR_BYTES; i++)
    {
        uwb_last_rx_hdr[i] = (i < n) ? snap[i] : 0U;
    }

    if (len == TWR_POLL_FRAME_LEN)
    {
        uwb_rx_len13_count++;
    }
    if (len >= 10U && snap[MAC_OFF_MSG_TYPE] == TWR_MSG_POLL)
    {
        uwb_rx_poll_match_count++;
    }
    if (len >= 11U && snap[MAC_OFF_MSG_TYPE] == TWR_MSG_RESP)
    {
        uwb_rx_resp_match_count++;
    }
    if (len >= 5U && snap[MAC_OFF_PAN_LO] == 0x11U && snap[MAC_OFF_PAN_LO + 1U] == 0x11U)
    {
        uwb_rx_pan_match_count++;
    }
}

static void uwb_store_rx_diag(const dwt_rxdiag_t *diag, int is_final, int is_poll)
{
    int16_t pdoa_val;
    int16_t direct;

    if (diag == NULL)
    {
        return;
    }

    direct = dwt_readpdoa();
    pdoa_val = direct;
    if (pdoa_val == 0)
    {
        pdoa_val = diag->pdoa;
    }

    uwb_last_rx_pdoa_raw = pdoa_val;
    uwb_last_ipatov_poa = uwb_poa_to_signed(diag->ipatovPOA);
    uwb_last_sts_poa = uwb_poa_to_signed(diag->stsPOA);
    uwb_last_sts2_poa = uwb_poa_to_signed(diag->sts2POA);
    uwb_last_sts_acc = diag->stsAccumCount;
    uwb_last_pdoa_direct = dwt_readpdoa();
    uwb_last_pdoa_mode = (uint8_t) ((dwt_read32bitreg(SYS_CFG_ID) & SYS_CFG_PDOA_MODE_BIT_MASK)
                                    >> SYS_CFG_PDOA_MODE_BIT_OFFSET);

    if (is_final)
    {
        uwb_final_pdoa = pdoa_val;
        uwb_final_ipatov_poa = uwb_poa_to_signed(diag->ipatovPOA);
        uwb_final_sts_poa = uwb_poa_to_signed(diag->stsPOA);
    }

    if (is_poll && pdoa_val != 0)
    {
        uwb_poll_pdoa_raw = pdoa_val;
    }

    if (pdoa_val != 0)
    {
        uwb_range_pdoa_raw = pdoa_val;
    }
}

static int uwb_frame_is_poll(uint16_t len, uint8_t msg_type)
{
    return (len == TWR_POLL_FRAME_LEN) || (msg_type == TWR_MSG_POLL);
}

static void uwb_latch_pdoa_after_poll_rx(uint16_t len)
{
    dwt_rxdiag_t diag;

    if (uwb_pdoa_lna_enable == 0U)
    {
        return;
    }
    if (app.pConfig == NULL || app.pConfig->s.userConfig.role != 1)
    {
        return;
    }
    if (len != TWR_POLL_FRAME_LEN)
    {
        return;
    }

    memset(&diag, 0, sizeof(diag));
    dwt_readdiagnostics(&diag);
    uwb_last_pdoa_pre = diag.pdoa;
    uwb_store_rx_diag(&diag, 0, 1);
}

void uwb_pdoa_latch_on_node_rx(void)
{
    uint16_t finfo;
    uint16_t len;

    finfo = dwt_read16bitoffsetreg(RX_FINFO_ID, 0);
    len = finfo & RX_FINFO_RXFLEN_BIT_MASK;
    uwb_latch_pdoa_after_poll_rx(len);
}

void uwb_status_sample(void)
{
    uint32_t st = dwt_read32bitreg(SYS_STATUS_ID);

    dwt_status_last = st;
    if (st & SYS_STATUS_TXFRS_BIT_MASK)
    {
        dwt_status_txfrs_count++;
    }
    if (st & SYS_STATUS_RXFCG_BIT_MASK)
    {
        dwt_status_rxfcg_count++;
    }
    if (st & SYS_STATUS_RXFR_BIT_MASK)
    {
        dwt_status_rxfr_count++;
    }
    if (st & SYS_STATUS_RXFTO_BIT_MASK)
    {
        dwt_status_rxto_count++;
    }
    if (st & SYS_STATUS_ALL_RX_ERR)
    {
        dwt_status_rxerr_count++;
    }
}

void __wrap_process_deca_irq(void)
{
    uint32_t st = dwt_read32bitreg(SYS_STATUS_ID);
    int had_rx = ((st & SYS_STATUS_RXFCG_BIT_MASK) != 0U) ? 1 : 0;
    uint16_t rx_len = 0U;

    if (had_rx)
    {
        uint16_t finfo = dwt_read16bitoffsetreg(RX_FINFO_ID, 0);
        uint8_t msg_type;

        rx_len = finfo & RX_FINFO_RXFLEN_BIT_MASK;
        if (rx_len >= (UWB_DATA_HDR_LEN + 3U)
            && rx_len != TWR_POLL_FRAME_LEN
            && rx_len != 21U
            && rx_len != TWR_FINAL_FRAME_LEN)
        {
            dwt_readrxdata(&msg_type, 1U, MAC_OFF_MSG_TYPE);
            if (msg_type == UWB_DATA_MSG_TYPE)
            {
                uwb_data_on_rx_frame(rx_len);
            }
        }
    }

    __real_process_deca_irq();

    if (had_rx)
    {
        uwb_latch_pdoa_after_poll_rx(rx_len);
    }

    uwb_status_sample();
}

int __wrap_dwt_configure(dwt_config_t *config)
{
    int rv;
    int pdoa_active = 0;

    if (uwb_pdoa_lna_enable != 0U)
    {
        pdoa_active = 1;
    }
    else if (app.pConfig != NULL && app.pConfig->s.s_pdoa.user_cmd != 0U)
    {
        pdoa_active = 1;
    }

    if (config != NULL && pdoa_active)
    {
        config->pdoaMode = DWT_PDOA_M3;
        config->stsMode = (uint8_t) (DWT_STS_MODE_1 | DWT_STS_MODE_SDC);
        config->stsLength = DWT_STS_LEN_64;
    }

    rv = __real_dwt_configure(config);
    if (rv == DWT_SUCCESS)
    {
        dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);
    }

    return rv;
}

void node_read_state_diag(uint8_t *state, uint8_t *done, uint8_t *rx_flag)
{
    uint8_t *node = (uint8_t *) getTwrInfoPtr();

    if (state != NULL)
    {
        *state = (node != NULL) ? node[NODE_OFF_STATE] : 0U;
    }
    if (done != NULL)
    {
        *done = (node != NULL) ? node[NODE_OFF_DONE] : 0U;
    }
    if (rx_flag != NULL)
    {
        *rx_flag = (node != NULL) ? node[NODE_OFF_RX_FLAG] : 0U;
    }
}
