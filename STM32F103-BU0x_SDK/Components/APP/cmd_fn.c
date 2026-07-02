/*
 * @file     cmd_fn.c
 * @brief    AT command processing
 * @author   Ai-Thinker
 * @date    2024.5.13
 */

#include "cmd_fn.h"
#include "user_out.h"
#include "uwb_pdoa.h"
#include "uwb_data.h"
#include "deca_version.h"
#include "config.h"
#include "node.h"
#include "Generic.h"
#include "Generic_cmd.h"
#include "lis2dh12.h"
#include "OLED_I2C.h"
#include "shared_functions.h"
#include "user_out.h"
#include "deca_device_api.h"
#include "uwb.h"

extern void tag_start (void);
static int cmd_fn_apply_rng_offset_mm (int dist_mm);
extern volatile uint8_t uwb_ranging_active;  /* defined in stm32f10x_it.c */
extern volatile uint8_t tag_bootstrap_pending; /* defined in tag_bootstrap.c */
extern volatile uint32_t tag_bootstrap_count;
extern void tag_read_scheduler_diag(uint8_t *state, uint8_t *poll_gate, uint8_t *slot_armed);
extern void tag_sync_poll_dest(void);
extern void tag_sync_twr_timing(void);
extern void tag_bootstrap_slot_scheduler(void);
extern uint8_t tag_read_resp_flag(void);
extern void node_read_state_diag(uint8_t *state, uint8_t *done, uint8_t *rx_flag);
extern volatile uint32_t dwt_status_txfrs_count;
extern volatile uint32_t dwt_status_rxfcg_count;
extern volatile uint32_t dwt_status_rxfr_count;
extern volatile uint32_t dwt_status_rxto_count;
extern volatile uint32_t dwt_status_rxerr_count;
extern volatile uint32_t dwt_status_last;
extern volatile uint8_t uwb_last_rx_len;
extern volatile uint8_t uwb_last_rx_hdr[16];
extern volatile uint32_t uwb_rx_len13_count;
extern volatile uint32_t uwb_rx_poll_match_count;
extern volatile uint32_t uwb_rx_resp_match_count;
extern volatile uint32_t uwb_rx_pan_match_count;
extern volatile uint8_t uwb_last_rx_b8;
extern volatile uint8_t uwb_last_rx_b9;
extern volatile int16_t uwb_last_rx_pdoa_raw;
extern volatile int16_t uwb_last_pdoa_pre;
extern volatile int16_t uwb_last_pdoa_post;
extern volatile int16_t uwb_last_pdoa_direct;
extern volatile int16_t uwb_last_ipatov_poa;
extern volatile int16_t uwb_last_sts_poa;
extern volatile int16_t uwb_last_sts2_poa;
extern volatile uint16_t uwb_last_sts_acc;
extern volatile uint8_t uwb_last_pdoa_mode;
extern volatile int16_t uwb_final_pdoa;
extern volatile int16_t uwb_final_ipatov_poa;
extern volatile int16_t uwb_final_sts_poa;

static void append_uint (char* dst, unsigned value)
{
    char tmp[16];
    int i = 0;

    if (value == 0)
    {
        strcat (dst, "0");
        return;
    }

    while (value > 0)
    {
        tmp[i++] = (char) ('0' + (value % 10));
        value /= 10;
    }

    while (i > 0)
    {
        char digit[2] = { tmp[--i], '\0' };
        strcat (dst, digit);
    }
}

/*
 * ????:??????
 *
 * */
int f_getver (int opt, int argc, char* argv[])
{
    char str[64];
    memset (str, 0, sizeof (str));

    strcpy (str, "\r\ngetver software:");
    strcat (str, uwb_software_ver);
    strcat (str, "\r\n");
    port_tx_msg (str, strlen (str));
    return 0;
}


/*
 * ????:????
 *
 * */
int f_save (int opt, int argc, char* argv[])
{
    sys_para.param_Config.s.userConfig.workmode = 2;
    App_Module_Sys_Write_NVM();
    port_tx_msg ((uint8_t*) "\r\nOK\r\n", 6);
    HalDelay_nMs (100);
    NVIC_SystemReset();
    return 0;
}



/*
 * ????:?????????
 *
 * */
int f_restore (int opt, int argc, char* argv[])
{
    App_Module_sys_para_Init();
    App_Module_Sys_Write_NVM();
    _dbg_printf ("\r\nOK\r\n");
    HalDelay_nMs (100);
    NVIC_SystemReset();
    return 0;
}


/*
 * ????:???????
 *
 * */
int f_restart (int opt, int argc, char* argv[])
{
    _dbg_printf ("\r\nOK\r\n");
    NVIC_SystemReset();
    return 0;
}


/*
 * Start UWB ranging without NVM reboot (stay in current session).
 */
/*
 * Stream the current distance value over USART1 (called periodically from SysTick).
 * Safe to call from interrupt context: USART1_SendBuffer is polled-TX with no malloc.
 */
extern uint32_t get_exti_irq_count(void);
extern uint32_t twr_output_cb_count;
extern volatile uint32_t dwt_tx_fail_count;
extern volatile uint32_t twr_tag_timeout_count;
extern volatile uint32_t twr_tag_tx_count;
extern volatile uint32_t twr_tag_rx_count;

static void append_int_field(char *str, int *i, const char *label, int value)
{
    char tmp[12];
    int j = 0;
    unsigned int u;
    int idx = *i;

    str[idx++] = ' ';
    for (const char *p = label; *p; p++)
    {
        str[idx++] = *p;
    }
    str[idx++] = ':';
    str[idx++] = ' ';

    if (value < 0)
    {
        str[idx++] = '-';
        u = (unsigned int) (-value);
    }
    else
    {
        u = (unsigned int) value;
    }

    if (u == 0U)
    {
        tmp[j++] = '0';
    }
    else
    {
        while (u > 0U)
        {
            tmp[j++] = (char) ('0' + (u % 10U));
            u /= 10U;
        }
    }
    for (int k = 0; k < j / 2; k++)
    {
        char t = tmp[k];
        tmp[k] = tmp[j - 1 - k];
        tmp[j - 1 - k] = t;
    }
    for (int k = 0; k < j; k++)
    {
        str[idx++] = tmp[k];
    }
    *i = idx;
}

static void append_uint_field(char *str, int *i, const char *label, unsigned int value)
{
    char tmp[12];
    int j = 0;
    int idx = *i;

    str[idx++] = ' ';
    for (const char *p = label; *p; p++)
    {
        str[idx++] = *p;
    }
    str[idx++] = ':';
    str[idx++] = ' ';

    if (value == 0)
    {
        tmp[j++] = '0';
    }
    else
    {
        unsigned int u = value;
        while (u > 0)
        {
            tmp[j++] = (char)('0' + (u % 10));
            u /= 10;
        }
    }
    for (int k = 0; k < j / 2; k++)
    {
        char t = tmp[k];
        tmp[k] = tmp[j - 1 - k];
        tmp[j - 1 - k] = t;
    }
    for (int k = 0; k < j; k++)
    {
        str[idx++] = tmp[k];
    }
    *i = idx;
}

extern volatile uint32_t twr_node_error_count;
extern volatile uint32_t twr_node_tx_count;
extern volatile uint32_t twr_node_rx_count;

static void append_hex16_field(char *str, int *i, const char *label, unsigned int value)
{
    static const char hex[] = "0123456789ABCDEF";
    int idx = *i;

    str[idx++] = ' ';
    for (const char *p = label; *p; p++)
    {
        str[idx++] = *p;
    }
    str[idx++] = ':';
    str[idx++] = ' ';
    str[idx++] = '0';
    str[idx++] = 'x';
    for (int shift = 12; shift >= 0; shift -= 4)
    {
        str[idx++] = hex[(value >> shift) & 0xFU];
    }
    *i = idx;
}

static void append_uwb_status_fields(char *str, int *i)
{
    append_uint_field(str, i, "txs", (unsigned int) dwt_status_txfrs_count);
    append_uint_field(str, i, "rxg", (unsigned int) dwt_status_rxfcg_count);
    append_uint_field(str, i, "rxf", (unsigned int) dwt_status_rxfr_count);
    append_uint_field(str, i, "rxt", (unsigned int) dwt_status_rxto_count);
    append_uint_field(str, i, "rxe", (unsigned int) dwt_status_rxerr_count);
    append_hex16_field(str, i, "sl", (unsigned int) (dwt_status_last & 0xFFFFU));
}

static void append_rx_sniff_fields(char *str, int *i)
{
    static const char hex[] = "0123456789ABCDEF";
    int idx = *i;
    int n = 13;

    append_uint_field(str, i, "rlen", (unsigned int) uwb_last_rx_len);
    append_uint_field(str, i, "l13", (unsigned int) uwb_rx_len13_count);
    append_uint_field(str, i, "pol", (unsigned int) uwb_rx_poll_match_count);
    append_uint_field(str, i, "rsp", (unsigned int) uwb_rx_resp_match_count);
    append_uint_field(str, i, "pan", (unsigned int) uwb_rx_pan_match_count);
    append_uint_field(str, i, "b8", (unsigned int) uwb_last_rx_b8);
    append_uint_field(str, i, "b9", (unsigned int) uwb_last_rx_b9);
    append_uint_field(str, i, "rpf", (unsigned int) tag_read_resp_flag());

    idx = *i;
    str[idx++] = ' ';
    str[idx++] = 'r';
    str[idx++] = 'h';
    str[idx++] = ':';
    str[idx++] = ' ';
    for (int b = 0; b < n; b++)
    {
        uint8_t v = uwb_last_rx_hdr[b];
        str[idx++] = hex[(v >> 4) & 0xFU];
        str[idx++] = hex[v & 0xFU];
    }
    *i = idx;
}

void f_stream_twr_diag(void)
{
    char str[512];
    int i = 0;
    char tmp[12];
    int j;
    unsigned int u;

    if (sys_para.param_Config.s.userConfig.role == 1)
    {
        const char *labels[] = {"nrx", "ntx", "ner", "st", "dn", "rf"};
        uint8_t st = 0, dn = 0, rf = 0;
        node_read_state_diag(&st, &dn, &rf);
        unsigned int vals[] = {
            (unsigned int) twr_node_rx_count,
            (unsigned int) twr_node_tx_count,
            (unsigned int) twr_node_error_count,
            (unsigned int) st,
            (unsigned int) dn,
            (unsigned int) rf,
        };
        str[i++] = 'a'; str[i++] = 'n'; str[i++] = 'c'; str[i++] = ':';
        for (int n = 0; n < 6; n++)
        {
            str[i++] = ' ';
            for (const char *p = labels[n]; *p; p++)
            {
                str[i++] = *p;
            }
            str[i++] = ':'; str[i++] = ' ';
            j = 0;
            u = vals[n];
            if (u == 0)
            {
                tmp[j++] = '0';
            }
            else
            {
                while (u > 0)
                {
                    tmp[j++] = (char)('0' + (u % 10));
                    u /= 10;
                }
            }
            for (int k = 0; k < j / 2; k++)
            {
                char t = tmp[k];
                tmp[k] = tmp[j - 1 - k];
                tmp[j - 1 - k] = t;
            }
            for (int k = 0; k < j; k++)
            {
                str[i++] = tmp[k];
            }
        }
        append_uwb_status_fields(str, &i);
        append_uint_field(str, &i, "txf", (unsigned int) dwt_tx_fail_count);
        append_uint_field(str, &i, "pdm", (unsigned int) uwb_last_pdoa_mode);
        append_int_field(str, &i, "pdpr", (int) uwb_last_pdoa_pre);
        append_int_field(str, &i, "pdor", (int) uwb_last_rx_pdoa_raw);
        append_int_field(str, &i, "pdop", (int) uwb_last_pdoa_post);
        append_int_field(str, &i, "pdir", (int) uwb_last_pdoa_direct);
        append_int_field(str, &i, "ipoa", (int) uwb_last_ipatov_poa);
        append_int_field(str, &i, "spoa", (int) uwb_last_sts_poa);
        append_int_field(str, &i, "s2poa", (int) uwb_last_sts2_poa);
        append_uint_field(str, &i, "stac", (unsigned int) uwb_last_sts_acc);
        append_int_field(str, &i, "fpdo", (int) uwb_final_pdoa);
        append_int_field(str, &i, "fipo", (int) uwb_final_ipatov_poa);
        append_int_field(str, &i, "fspo", (int) uwb_final_sts_poa);
        append_rx_sniff_fields(str, &i);
        str[i++] = '\r'; str[i++] = '\n';
        USART1_SendBuffer(str, (uint16_t) i, 1);
        user_out_stream_pdoa_on_anchor_tick();
        return;
    }

    f_stream_distance();
}

void f_stream_distance (void)
{
    char str[360];
    int i = 0;
    int dist_mm;

    /* Anchor TX timing is tight; keep USART1 quiet on the responder. */
    if (sys_para.param_Config.s.userConfig.role == 1)
    {
        return;
    }

    dist_mm = (int) (distance * 1000.0f + 0.5f);
    dist_mm = cmd_fn_apply_rng_offset_mm (dist_mm);
    if (dist_mm < 0)
    {
        dist_mm = 0;
    }

    /* Build "distance: NNN irq: KKK\r\n" */
    str[i++] = 'd'; str[i++] = 'i'; str[i++] = 's'; str[i++] = 't';
    str[i++] = 'a'; str[i++] = 'n'; str[i++] = 'c'; str[i++] = 'e';
    str[i++] = ':'; str[i++] = ' ';

    char tmp[12];
    int j = 0;
    unsigned int u = (unsigned int) dist_mm;
    if (u == 0)
    {
        tmp[j++] = '0';
    }
    else
    {
        while (u > 0) { tmp[j++] = (char)('0' + (u % 10)); u /= 10; }
    }
    /* reverse */
    for (int k = 0; k < j / 2; k++)
    {
        char t = tmp[k]; tmp[k] = tmp[j-1-k]; tmp[j-1-k] = t;
    }
    for (int k = 0; k < j; k++) str[i++] = tmp[k];

    /* Append irq field */
    str[i++] = ' '; str[i++] = 'i'; str[i++] = 'r'; str[i++] = 'q';
    str[i++] = ':'; str[i++] = ' ';

    j = 0;
    u = (unsigned int) get_exti_irq_count();
    if (u == 0)
    {
        tmp[j++] = '0';
    }
    else
    {
        while (u > 0) { tmp[j++] = (char)('0' + (u % 10)); u /= 10; }
    }
    for (int k = 0; k < j / 2; k++)
    {
        char t = tmp[k]; tmp[k] = tmp[j-1-k]; tmp[j-1-k] = t;
    }
    for (int k = 0; k < j; k++) str[i++] = tmp[k];

    /* Append twr callback count */
    str[i++] = ' '; str[i++] = 'c'; str[i++] = 'b';
    str[i++] = ':'; str[i++] = ' ';

    j = 0;
    u = (unsigned int) twr_output_cb_count;
    if (u == 0)
    {
        tmp[j++] = '0';
    }
    else
    {
        while (u > 0) { tmp[j++] = (char)('0' + (u % 10)); u /= 10; }
    }
    for (int k = 0; k < j / 2; k++)
    {
        char t = tmp[k]; tmp[k] = tmp[j-1-k]; tmp[j-1-k] = t;
    }
    for (int k = 0; k < j; k++) str[i++] = tmp[k];

    append_uint_field(str, &i, "txf", (unsigned int) dwt_tx_fail_count);
    append_uint_field(str, &i, "tto", (unsigned int) twr_tag_timeout_count);
    append_uint_field(str, &i, "ptx", (unsigned int) twr_tag_tx_count);
    append_uint_field(str, &i, "prx", (unsigned int) twr_tag_rx_count);
    append_uint_field(str, &i, "bst", (unsigned int) tag_bootstrap_count);
    {
        uint8_t st = 0, pg = 0, sa = 0;
        tag_read_scheduler_diag(&st, &pg, &sa);
        append_uint_field(str, &i, "st", (unsigned int) st);
        append_uint_field(str, &i, "pg", (unsigned int) pg);
        append_uint_field(str, &i, "sa", (unsigned int) sa);
    }
    append_uwb_status_fields(str, &i);
    append_rx_sniff_fields(str, &i);

    str[i++] = '\r'; str[i++] = '\n';

    USART1_SendBuffer (str, i, 1);
}

int f_uwbstart (int opt, int argc, char* argv[])
{
    port_tx_msg ((uint8_t*) "\r\nOK\r\n", 6);
    HalDelay_nMs (50);
    if (sys_para.param_Config.s.s_pdoa.user_cmd != 0U)
    {
        set_aiio_output_state (false);
    }
    else
    {
        set_aiio_output_state (true);
    }

    if (sys_para.flag != 0xAAAA)
    {
        port_tx_msg ((uint8_t*) "UWBSTART: NVM not ready (flag != 0xAAAA)\r\n", 42);
        return 0;
    }

    /* Lightweight SPI sanity check: verify DW3000 is reachable before handing off
     * to node_start()/tag_start(), which do their own full reset+init sequence.
     * Do NOT call dwt_initialise() here — node_process_init() calls it internally,
     * and a second call would reset all TX/RX callbacks to NULL, breaking ranging. */
    port_set_dw_ic_spi_slowrate();

    {
        uint32_t devid = dwt_readdevid();
        char dbuf[24];
        int di = 0;
        dbuf[di++]='D'; dbuf[di++]='E'; dbuf[di++]='V'; dbuf[di++]='I';
        dbuf[di++]='D'; dbuf[di++]=':'; dbuf[di++]='0'; dbuf[di++]='x';
        for (int s = 28; s >= 0; s -= 4) {
            int n = (devid >> s) & 0xF;
            dbuf[di++] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
        }
        dbuf[di++] = '\r'; dbuf[di++] = '\n';
        USART1_SendBuffer(dbuf, (uint16_t)di, 1);

        if (devid == 0xFFFFFFFF || devid == 0x00000000)
        {
            port_tx_msg ((uint8_t*) "UWBSTART: DW3000 SPI not responding\r\n", 37);
            return 0;
        }
    }

    if (sys_para.param_Config.s.userConfig.twr_pdoa_mode == 1)
    {
        uwb_ranging_active = 1;
        if (sys_para.param_Config.s.userConfig.role == 1)
        {
            ds_twr_sts_sdc_responder();
        }
        else if (sys_para.param_Config.s.userConfig.role == 0)
        {
            ds_twr_sts_sdc_initiator();
        }
        return 0;
    }

    /* BU04 dual-RX PDOA: prepare config before handing off to node.o / tag.o. */
    uwb_pdoa_prepare_config(&sys_para.param_Config);
    if (app.pConfig != NULL)
    {
        uwb_pdoa_prepare_config(app.pConfig);
    }
    uwb_data_init();

    if (sys_para.param_Config.s.userConfig.role == 1)
    {
        uwb_ranging_active = 1;
        node_start();
        uwb_pdoa_apply_after_start(1U);
        port_set_dw_ic_spi_fastrate();
    }
    else if (sys_para.param_Config.s.userConfig.role == 0)
    {
        /* Precompiled tag TWR uses userConfig.nodeAddr for poll destination at init.
         * s_pdoa.addr (PDOASETCFG anc_id) is never read by tag.o — sync poll target here. */
        uint16_t anc = (uint16_t) sys_para.param_Config.s.s_pdoa.addr;
        if (anc != 65535U && anc != sys_para.param_Config.s.userConfig.nodeAddr)
        {
            /* Stash tag id in s_pdoa.addr temporarily is wrong; overwrite nodeAddr used
             * only if anc_id differs — for standard setup both are 0 anyway. */
            (void) anc;
        }
        if (sys_para.param_Config.s.userConfig.nodePanid != sys_para.param_Config.s.s_pdoa.panID)
        {
            sys_para.param_Config.s.userConfig.nodePanid = sys_para.param_Config.s.s_pdoa.panID;
            if (app.pConfig != NULL)
            {
                app.pConfig->s.userConfig.nodePanid = sys_para.param_Config.s.s_pdoa.panID;
            }
        }

        /* uint16_t field — stay within 65535. Vendor default is 20000 us. */
        sys_para.param_Config.s.sfConfig.tag_pollTxFinalTx_us = 20000;
        sys_para.param_Config.s.sfConfig.tag_replyDly_us = DEFAULT_TAG_POLL_TX_RESPONSE_RX_US;
        sys_para.param_Config.s.s_pdoa.sfConfig_pdoa.tag_pollTxFinalTx_us = 20000;
        sys_para.param_Config.s.s_pdoa.sfConfig_pdoa.tag_replyDly_us = DEFAULT_TAG_POLL_TX_RESPONSE_RX_US;
        if (app.pConfig != NULL)
        {
            app.pConfig->s.sfConfig.tag_pollTxFinalTx_us = 20000;
            app.pConfig->s.sfConfig.tag_replyDly_us = DEFAULT_TAG_POLL_TX_RESPONSE_RX_US;
            app.pConfig->s.s_pdoa.sfConfig_pdoa.tag_pollTxFinalTx_us = 20000;
            app.pConfig->s.s_pdoa.sfConfig_pdoa.tag_replyDly_us = DEFAULT_TAG_POLL_TX_RESPONSE_RX_US;
        }
        {
            char tbuf[96];
            int ti = sprintf(tbuf,
                             "TWR cfg: tag=%u anc=%u pan=%04X final=%u reply=%u\r\n",
                             (unsigned) sys_para.param_Config.s.userConfig.nodeAddr,
                             (unsigned) sys_para.param_Config.s.s_pdoa.addr,
                             (unsigned) sys_para.param_Config.s.userConfig.nodePanid,
                             (unsigned) sys_para.param_Config.s.sfConfig.tag_pollTxFinalTx_us,
                             (unsigned) sys_para.param_Config.s.sfConfig.tag_replyDly_us);
            USART1_SendBuffer(tbuf, (uint16_t) ti, 1);
        }
        uwb_ranging_active = 1;
        tag_bootstrap_pending = 1;
        tag_start();
        uwb_pdoa_apply_after_start(0U);
        tag_sync_poll_dest();
        tag_sync_twr_timing();
        tag_bootstrap_slot_scheduler();
        tag_bootstrap_pending = 0;
        /* Runtime TWR needs fast SPI for timestamp reads; must follow tag_start init. */
        port_set_dw_ic_spi_fastrate();
    }

    return 0;
}


/*
 * ????:???????????
 *
 * */
int f_setcfg (int opt, int argc, char* argv[])
{
    if (opt == SET_CMD)
    {
        char str[256];
        memset (str, 0, sizeof (str));
        uint8_t role = 0, ch = 0, rate = 0;
        uint16_t id = 0;

        id = atoi (argv[0]);
        role = atoi (argv[1]);
        ch = atoi (argv[2]);
        rate = atoi (argv[3]);

        if ( (role == 0 || role == 1) &&
                (ch == 0  || ch == 1)   &&
                (rate == 0 || rate == 1) &&
                (argc == 4))
        {
            sys_para.param_Config.s.userConfig.role = role;

            if (ch == 0)
            {
                sys_para.param_Config.dwt_config.chan = 9;
            }
            else
            {
                sys_para.param_Config.dwt_config.chan = 5;
            }

            if (rate == 1)
            {
                sys_para.param_Config.dwt_config.dataRate = DWT_BR_6M8;
            }
            else
            {
                sys_para.param_Config.dwt_config.dataRate = DWT_BR_850K;
            }

            if (sys_para.param_Config.s.userConfig.role == 1) //node
            {
                if ( (id <= MAX_ANCHOR_LIST_SIZE))
                {
                    sys_para.param_Config.s.userConfig.nodeAddr = id;
                    sys_para.param_Config.s.s_pdoa.addr = id;
                    sprintf (str, "\r\nsetcfg ID:%d, Role:%d, CH:%d, Rate:%d\r\n",
                             sys_para.param_Config.s.userConfig.nodeAddr, sys_para.param_Config.s.userConfig.role, ch, rate);
                    port_tx_msg (str, strlen (str));
                    _dbg_printf ("\r\nOK\r\n");
                }
            }
            else  //tag
            {
                sys_para.param_Config.s.userConfig.nodeAddr = id;
                sys_para.param_Config.s.s_pdoa.addr = id;

                sprintf (str, "\r\nsetcfg ID:%d, Role:%d, CH:%d, Rate:%d\r\n",
                         sys_para.param_Config.s.userConfig.nodeAddr, sys_para.param_Config.s.userConfig.role, ch, rate);
                port_tx_msg (str, strlen (str));
                _dbg_printf ("\r\nOK\r\n");
            }

            return 0;
        }
        else
        {
            return -1;
        }
    }
}

/*
 * ????:??????????
 *
 * */
int f_getcfg (int opt, int argc, char* argv[])
{
    uint8_t ch = 0, rate = 0;
    char str[256];
    memset (str, 0, sizeof (str));

    if (sys_para.param_Config.dwt_config.chan == 9)
    {
        ch = 0;
    }
    else
    {
        ch = 1;
    }

    if (sys_para.param_Config.dwt_config.dataRate == DWT_BR_6M8)
    {
        rate = 1;
    }
    else
    {
        rate = 0;
    }

    memset (str, 0, sizeof (str));
    strcpy (str, "\r\ngetcfg ID:");
    append_uint (str, sys_para.param_Config.s.userConfig.nodeAddr);
    strcat (str, ", Role:");
    append_uint (str, sys_para.param_Config.s.userConfig.role);
    strcat (str, ", CH:");
    append_uint (str, ch);
    strcat (str, ", Rate:");
    append_uint (str, rate);
    strcat (str, "\r\n");
    port_tx_msg (str, strlen (str));
    return 0;
}

/*
 * ????:??????????
 *
 * */
int f_setworkmode (int opt, int argc, char* argv[])
{
    char str[64];
    memset (str, 0, sizeof (str));
    uint8_t mode = 0;

    mode = atoi (argv[0]);

    if ( (mode == 0 || mode == 1) &&
            (argc == 1))
    {
        sys_para.param_Config.s.userConfig.workmode = mode;

        if (sys_para.param_Config.s.userConfig.workmode == 1)
        {
            OLED_CLS();
            //HalLed_Mode_Set(HAL_LED1, HAL_LED_MODE_OFF, Sys_mode_led_blink_invalid);
            //dwt_setleds(DWT_LEDS_DISABLE | DWT_LEDS_INIT_BLINK) ;
        }

        sprintf (str, "\r\nworkmode: %d\r\n", sys_para.param_Config.s.userConfig.workmode);
        port_tx_msg (str, strlen (str));
#ifdef JOHHN
#else
        App_Module_Sys_Write_NVM();
        _dbg_printf ("\r\nOK\r\n");
        HalDelay_nMs (100);
        NVIC_SystemReset();
#endif
        return 0;
    }

    return -1;
}

/*
 * ????:?????????
 *
 * */
int f_getworkmode (int opt, int argc, char* argv[])
{
    char str[64];
    memset (str, 0, sizeof (str));

    sprintf (str, "\r\nworkmode: %d\r\n", sys_para.param_Config.s.userConfig.workmode);
    port_tx_msg (str, strlen (str));
    return 0;
}

/*
 * ????:???????????
 *
 * */
int f_setdev (int opt, int argc, char* argv[])
{
    uint8_t str[256];
    memset (str, 0, sizeof (str));

    //(??????????,???????,???????,??????Q,??????R,???????a,???????b,????????,???????)
    uint8_t cap = 0, kalman_enable = 0, loc_enable = 0, loc_dimen = 0;
    float kalman_Q, kalman_R, para_a, para_b;
    int antdelay = 0;

    cap = atoi (argv[0]);
    kalman_enable = atoi (argv[2]);
    loc_enable = atoi (argv[7]);
    loc_dimen = atoi (argv[8]);
    kalman_Q = atof (argv[3]);
    kalman_R = atof (argv[4]);
    para_a = atof (argv[5]);
    para_b = atof (argv[6]);
    antdelay = atoi (argv[1]);

    if ( (argc == 9))
    {
        app.pConfig->s.userConfig.tagnumSlots = cap;            //???????
        app.pConfig->s.baseConfig.antRx = antdelay;         //???????
        app.pConfig->s.baseConfig.antTx = antdelay;         //???????
        app.pConfig->s.userConfig.nodeFilter = kalman_enable;   //??????
        app.pConfig->s.userConfig.kalman_Q = kalman_Q;      //??????Q
        app.pConfig->s.userConfig.kalman_R = kalman_R;      //??????R
        app.pConfig->s.userConfig.para_a = para_a;          //?????a
        app.pConfig->s.userConfig.para_b = para_b;          //?????b

//      instance_config_antennadelays(sys_para.dist.AntennaDelay,sys_para.dist.AntennaDelay);
//
//
        sprintf (str, "\r\nsetdev cap:%d anndelay:%d, kalman_enable:%d, kalman_Q:%.3f, kalman_R:%.3f, para_a:%.4f, para_b:%.2f, pos_enable:%d, pos_dimen:%d\r\n",
                 app.pConfig->s.userConfig.tagnumSlots, app.pConfig->s.baseConfig.antRx, app.pConfig->s.userConfig.nodeFilter,
                 app.pConfig->s.userConfig.kalman_Q, app.pConfig->s.userConfig.kalman_R,
                 app.pConfig->s.userConfig.para_a, app.pConfig->s.userConfig.para_b,
                 0, 0);

        port_tx_msg (str, strlen (str));
        return 0;
    }

    return -1;
}


/*
 * ????:???????????
 *
 * */
int f_getdev (int opt, int argc, char* argv[])
{
    char str[256];
    memset (str, 0, sizeof (str));

    strcpy (str, "\r\ngetdev cap:");
    append_uint (str, app.pConfig->s.userConfig.tagnumSlots);
    strcat (str, " anndelay:");
    append_uint (str, app.pConfig->s.baseConfig.antRx);
    strcat (str, ", kalman_enable:");
    append_uint (str, app.pConfig->s.userConfig.nodeFilter);
    /* Float %f is broken in GCC nano printf; emit integer placeholders. */
    strcat (str, ", kalman_Q:0, kalman_R:0, para_a:0, para_b:0, pos_enable:0, pos_dimen:0\r\n");
    port_tx_msg (str, strlen (str));
    return 0;
}

/*
 *  ?????AT??????
 *
 * */
int f_test (int opt, int argc, char* argv[])
{
    return 0;
}

/*
 *  ?????????????????????
 *
 * */
int f_getsensor (int opt, int argc, char* argv[])
{
    char str[64];
    float speed = 0;
    int ret = 0;
    memset (str, 0, sizeof (str));

    ret = drv_lis2dh12_get_angle (&speed);

    if (ret != 0)
    {
        return -1;
    }

    sprintf (str, "\r\nangle: %f\r\n", speed);
    port_tx_msg (str, strlen (str));
    return 0;
}


/*
 *  ?????led????
 *
 * */
int f_testled (int opt, int argc, char* argv[])
{
    char str[64];
    memset (str, 0, sizeof (str));
    uint8_t mode = 0;

    mode = atoi (argv[0]);

    if ( (mode == 0 || mode == 1) &&
            (argc == 1))
    {
        if (mode == 1)
        {
            HalLed_Mode_Set (HAL_LED1, HAL_LED_MODE_OFF, Sys_mode_led_blink_invalid);
            led_test = true;
        }
        else
        {
            led_test = false;
        }

        flow_light (led_test);
    }

    return 0;
}

/*
 *  ????????\r\n???
 *
 * */

void remove_newline_chars (char* str)
{
    char* src = str;
    char* dst = str;

    while (*src)
    {
        if (*src != '\r' && *src != '\n')
        {
            *dst = *src;
            dst++;
        }

        src++;
    }

    *dst = '\0';
}

/*
 *  ??????????????
 *
 * */
int f_testoled (int opt, int argc, char* argv[])
{
    remove_newline_chars (argv[0]);

    if (argc == 1)
    {
        OLED_CLS();
        OLED_ShowStr (19, 3, argv[0], 2);
    }

    return 0;
}

float distance = 0;

/* AT+RNGOFF stores rngOffset_mm in NVM; apply it here so AT+DISTANCE and the
 * streamed "distance:" line report the calibrated range. */
static int cmd_fn_apply_rng_offset_mm (int dist_mm)
{
    int16_t offset_mm = (app.pConfig != NULL) ? app.pConfig->s.s_pdoa.rngOffset_mm : 0;
    int corrected_mm = dist_mm + (int) offset_mm;
    return (corrected_mm < 0) ? 0 : corrected_mm;
}

/*
 *  ????????
 *
 * */
int f_distance (int opt, int argc, char* argv[])
{
    char str[64];
    int dist_mm;
    memset (str, 0, sizeof (str));

    dist_mm = (int) (distance * 1000.0f + 0.5f);
    dist_mm = cmd_fn_apply_rng_offset_mm (dist_mm);
    if (dist_mm < 0)
    {
        dist_mm = 0;
    }

    strcpy (str, "\r\ndistance: ");
    append_uint (str, (unsigned) dist_mm);
    strcat (str, "\r\n");
    port_tx_msg (str, strlen (str));
    return 0;
}

/*PDOA*/
/*
 * ????:?????????????
 *
 * */
int f_deca (int opt, int argc, char* argv[])
{
    char str[256];
    memset (str, 0, sizeof (str));

    int  hlen;
    hlen = sprintf (str, "JS%04X", 0x5A5A);  // reserve space for length of JS object

    sprintf (&str[strlen (str)], "{\"Info\":{\r\n");
    sprintf (&str[strlen (str)], "\"Device\":\"PDOA Node\",\r\n");
    sprintf (&str[strlen (str)], "\"Version\":\"%s\",\r\n", uwb_software_ver);
    sprintf (&str[strlen (str)], "\"Build\":\"%s %s\",\r\n", __DATE__, __TIME__);
    sprintf (&str[strlen (str)], "\"Driver\":\"%s\"}}", DW3000_DEVICE_DRIVER_VER_STRING);

    sprintf (&str[2], "%04X", strlen (str) - hlen); //add formatted 4X of length, this will erase first '{'
    str[hlen] = '{';                          //restore the start bracket
    sprintf (&str[strlen (str)], "\r\n");
    port_tx_msg ( (uint8_t*) str, strlen (str));
    return 0;
}

/*
 * ????????[????????]
 *
 * */
int f_getdlist (int opt, int argc, char* argv[])
{
    uint64_t*    pAddr64;
    uint16_t     size;
    int          jlen, tmp;
    int offset = 0;

    pAddr64     = getDList();
    size        = getDList_size();

    char str[MAX_STR_SIZE];
    memset (str, 0, sizeof (str));

    jlen = (11 + 2);

    if (size > 0)
    {
        tmp = 0;
        tmp = strlen ("\"1122334455667788\"");   //16+2 for every addr64
        jlen += (tmp + 3) * size - 3;
    }

    sprintf (str, "JS%04X", jlen);                 // print pre-calculated length of JS object

    offset += snprintf (str + offset, sizeof (str) - offset, "{\"DList\":[ ");

    //DList cannot be with gaps
    // if changed, will need to change the calculation of jlen
    while (size > 0)
    {
        offset += snprintf (str + offset, sizeof (str) - offset, "\"%08lX%08lX\"", (uint32_t) (*pAddr64 >> 32), (uint32_t) (*pAddr64));

        if (size > 1)
        {
            offset += snprintf (str + offset, sizeof (str) - offset, ",\r\n");
        }

        pAddr64++;
        size--;
    }

    offset += snprintf (str + offset, sizeof (str) - offset, "]}");
    port_tx_msg ( (uint8_t*) str, offset);

    initDList();                                    //clear the Discovered list

    return 0;
}

static void fill_json_tag (char* str, tag_addr_slot_t* tag)
{
    sprintf (str,
             "{\"slot\":\"%04X\",\"a64\":\"%08lX%08lX\",\"a16\":\"%04X\","
             "\"F\":\"%04X\",\"S\":\"%04X\",\"M\":\"%04X\"}",
             tag->slot, (uint32_t) (tag->addr64 >> 32), (uint32_t) (tag->addr64), tag->addr16,
             tag->multFast, tag->multSlow, tag->mode);
}

/*
 *  ????????[???????]
 *
 * */
int f_getklist (int opt, int argc, char* argv[])
{
    tag_addr_slot_t*    tag;
    int                jlen, size, tmp;
    int offset = 0;

    char str[MAX_STR_SIZE];
    memset (str, 0, sizeof (str));

    /*  16 bytes overhead for JSON
       *  66 bytes per known tag
       *  3 bytes per separation
       *  we need to pre-calculate the length of json string for DList & KList : in order to keep malloc() small
       */
    tag = get_knownTagList();
    size = get_knownTagList_size();

    jlen = (10 + 2);

    if (size > 0)
    {
        tmp = 0;
        fill_json_tag (str, tag);
        tmp = strlen (str);                           //+NN to json for every known tag
        jlen += (tmp + 3) * size - 3;
    }

    sprintf (str, "JS%04X", jlen);                      // 6 print pre-calculated length of JS object
    offset += snprintf (str + offset, sizeof (str) - offset, "{\"KList\":[");

    //KList can be with gaps, so need to scan it whole
    for (int i = 0; i < MAX_KNOWN_TAG_LIST_SIZE; i++)
    {
        if (tag->slot != (uint16_t) (0))
        {
            fill_json_tag (str + offset, tag + i);                   //NN
            int tag_len = strlen (str + offset);
            offset += tag_len;

            if (size > 1)   //last element should not have ',\r\n'
            {
                offset += snprintf (str + offset, sizeof (str) - offset, ",\r\n");
            }

            size--;
        }

        tag++;
    }

    offset += snprintf (str + offset, sizeof (str) - offset, "]}");
    port_tx_msg ( (uint8_t*) str, offset);

    return 0;
}

/*
 *  ?????????????[???????]
 *
 * */
int f_addtag (int opt, int argc, char* argv[])
{
    char str[MAX_STR_SIZE];
    memset (str, 0, sizeof (str));

    tag_addr_slot_t*    tag;
    uint64_t           addr64 = 0;
    unsigned int       addr16 = 0, multFast = 0, multSlow = 0, mode = 0, n = 1, hlen;

    addr64 = strtoull (argv[0], NULL, 16);
    addr16 = strtoul (argv[1], NULL, 16);
    multFast = atoi (argv[2]);
    multSlow = atoi (argv[3]);
    mode = atoi (argv[4]);

    if (! (multFast == 0) && ! (multSlow == 0) && (argc == 5))
    {
        tag = add_tag_to_knownTagList (addr64, (uint16_t) addr16);

        if (tag)
        {
            tag->multFast = (uint16_t) multFast;
            tag->multSlow = (uint16_t) multSlow;
            tag->mode = (uint16_t) mode;

            hlen = sprintf (str, "JS%04X", 0x5A5A);  // reserve space for length of JS object
            sprintf (&str[strlen (str)], "{\"TagAdded\": ");
            fill_json_tag (&str[strlen (str)], tag);
            sprintf (&str[strlen (str)], "}"); //\r\n

            sprintf (&str[2], "%04X", strlen (str) - hlen); //add formatted 4X of length, this will kill first '{'
            str[hlen] = '{';                          //restore the start bracket

            sprintf (&str[strlen (str)], "\r\n");
            port_tx_msg ( (uint8_t*) str, strlen (str));

            return 0;
        }

        return 0;
    }

    return -1;
}

/*
 *  ?????????????[???????]
 *
 * */
int f_deltag (int opt, int argc, char* argv[])
{
    const char* ret = NULL;

    char str[MAX_STR_SIZE];
    memset (str, 0, sizeof (str));


    uint64_t        addr64 = 0;
    unsigned int    hlen;

    /* "delTag 11AABB4455FF7788" */
    addr64 = strtoull (argv[0], NULL, 16);

    if (argc == 1)
    {
        if (addr64 > 0xFFFF)
        {
            del_tag64_from_knownTagList (addr64);
        }
        else
        {
            del_tag16_from_knownTagList ( (uint16_t) addr64);
        }

        hlen = sprintf (str, "JS%04X", 0x5A5A);  // reserve space for length of JS object
        sprintf (&str[strlen (str)], "{\"TagDeleted\": \"%16llx\"}", addr64);

        sprintf (&str[2], "%04X", strlen (str) - hlen); //add formatted 4X of length, this will erase first '{'
        str[hlen] = '{';                          //restore the start bracket
        sprintf (&str[strlen (str)], "\r\n");
        port_tx_msg ( (uint8_t*) str, strlen (str));

        return 0;
    }

    return -1;
}

/*
 *  ?????????????
 *
 * */
int f_pdoaoff (int opt, int argc, char* argv[])
{
    if (argc == 1)
    {
        app.pConfig->s.s_pdoa.pdoaOffset_deg = (int16_t) atoi (argv[0]);
        return 0;
    }

    return -1;
}

/*
 *  ??????????????
 *
 * */
int f_rngoff (int opt, int argc, char* argv[])
{
    if (argc == 1)
    {
        app.pConfig->s.s_pdoa.rngOffset_mm = (int16_t) atoi (argv[0]);
        return 0;
    }

    return -1;
}

/*
 *  ?????????????
 *
 * */
int f_filter (int opt, int argc, char* argv[])
{
    if (argc == 1)
    {
        app.pConfig->s.s_pdoa.motionfilter  = (int16_t) atoi (argv[0]);
        return 0;
    }

    return -1;
}

/*
 *  ?????????????????(???????????????,?????????????,???PANID,??????ID,???????????)
 *
 * */
int f_pdoasetcfg (int opt, int argc, char* argv[])
{
    char str[256];
    uint16_t DiscList_num, BindList_num, panID, AncID, serail_Rate, filter, user_cmd;
    int n = 0;
    char tmp[10];
    memset (str, 0, sizeof (str));


    DiscList_num = atoi (argv[0]);
    BindList_num = atoi (argv[1]);
    panID = atoi (argv[2]);
    AncID = atoi (argv[3]);
    serail_Rate = atoi (argv[4]);
    filter = atoi (argv[5]);
    user_cmd = atoi (argv[6]);

    if ( (argc == 7) && DiscList_num <= MAX_DISCOVERED_TAG_LIST_SIZE &&
            BindList_num <= MAX_KNOWN_TAG_LIST_SIZE && AncID <= 1)
    {

//      pbss->s.Dlist = DiscList_num;
//      pbss->s.Klist = BindList_num;

        sys_para.param_Config.s.s_pdoa.addr = (int16_t) AncID;
        sys_para.param_Config.s.s_pdoa.panID = (uint16_t) (panID);
        sys_para.param_Config.s.s_pdoa.UartReFreshRate = serail_Rate;
        sys_para.param_Config.s.s_pdoa.motionfilter = filter;
        sys_para.param_Config.s.s_pdoa.user_cmd = user_cmd;
        sys_para.param_Config.s.s_pdoa.phaseCorrEn = (user_cmd != 0U) ? 1U : 0U;
        sys_para.param_Config.s.userConfig.nodePanid = (uint16_t) panID;
        if (app.pConfig != NULL)
        {
            app.pConfig->s.s_pdoa.addr = (int16_t) AncID;
            app.pConfig->s.s_pdoa.panID = (uint16_t) (panID);
            app.pConfig->s.s_pdoa.UartReFreshRate = serail_Rate;
            app.pConfig->s.s_pdoa.motionfilter = filter;
            app.pConfig->s.s_pdoa.user_cmd = user_cmd;
            app.pConfig->s.s_pdoa.phaseCorrEn = (user_cmd != 0U) ? 1U : 0U;
            app.pConfig->s.userConfig.nodePanid = (uint16_t) panID;
        }
        sprintf (str, "getcfg Dlist:%d KList:%d Net:%04X AncID:%d Rate:%d Filter:%d UserCmd:%d pdoaOffset:%d rngOffset:%d\r\n",
                 app.pConfig->s.s_pdoa.Dlist,
                 app.pConfig->s.s_pdoa.Klist,
                 app.pConfig->s.s_pdoa.panID,
                 app.pConfig->s.s_pdoa.addr,
                 app.pConfig->s.s_pdoa.UartReFreshRate,
                 app.pConfig->s.s_pdoa.motionfilter,
                 app.pConfig->s.s_pdoa.user_cmd,
                 app.pConfig->s.s_pdoa.pdoaOffset_deg,
                 app.pConfig->s.s_pdoa.rngOffset_mm);

        port_tx_msg (str, strlen (str));

        return 0;
    }

    return -1;
}


/*
 *  ????????????????(???????????????,?????????????,???PANID,??????ID,???????????)
 *
 * */
int f_pdoagetcfg (int opt, int argc, char* argv[])
{
    char str[256];
    memset (str, 0, sizeof (str));

    sprintf (str, "getcfg Dlist:%d KList:%d Net:%04X AncID:%d Rate:%d Filter:%d UserCmd:%d pdoaOffset:%d rngOffset:%d\r\n",
             app.pConfig->s.s_pdoa.Dlist,
             app.pConfig->s.s_pdoa.Klist,
             app.pConfig->s.s_pdoa.panID,
             app.pConfig->s.s_pdoa.addr,
             app.pConfig->s.s_pdoa.UartReFreshRate,
             app.pConfig->s.s_pdoa.motionfilter,
             app.pConfig->s.s_pdoa.user_cmd,
             app.pConfig->s.s_pdoa.pdoaOffset_deg,
             app.pConfig->s.s_pdoa.rngOffset_mm);

    port_tx_msg (str, strlen (str));

    return 0;
}

/*
 *  ???????????????
 *
 * */
int f_uart_rate (int opt, int argc, char* argv[])
{
    if (argc == 1)
    {
        app.pConfig->s.s_pdoa.UartReFreshRate   = (int16_t) atoi (argv[0]);
        return 0;
    }

    return -1;
}

/*
 *  ?????????????? 0:Json??? 1:Hex???
 *
 * */
int f_user_cmd (int opt, int argc, char* argv[])
{
    if (argc == 1)
    {
        int16_t user_cmd = (int16_t) atoi (argv[0]);

        app.pConfig->s.s_pdoa.user_cmd = user_cmd;
        app.pConfig->s.s_pdoa.phaseCorrEn = (user_cmd != 0U) ? 1U : 0U;
        sys_para.param_Config.s.s_pdoa.user_cmd = user_cmd;
        sys_para.param_Config.s.s_pdoa.phaseCorrEn = (user_cmd != 0U) ? 1U : 0U;
        return 0;
    }

    return -1;
}

/*
 * ????:????????
 *
 * */
int f_setuwbmode (int opt, int argc, char* argv[])
{
    uint8_t mode = 0;

    mode = atoi (argv[0]);

    if ( (mode == 0 || mode == 1) &&
            (argc == 1))
    {
        sys_para.param_Config.s.userConfig.twr_pdoa_mode = mode;
        if (app.pConfig != NULL)
        {
            app.pConfig->s.userConfig.twr_pdoa_mode = mode;
        }
        /* Fix: was port_tx_msg(str, strlen(str)) where str was always empty (memset 0). */
        port_tx_msg ((uint8_t*) "\r\nOK\r\n", 6);
        return 0;
    }

    return -1;
}

/*
 * ????:???????
 *
 * */
int f_getewbmode (int opt, int argc, char* argv[])
{
    char str[64];
    memset (str, 0, sizeof (str));

    sprintf (str, "\r\ntwr_pdoa_mode: %d\r\n", sys_para.param_Config.s.userConfig.twr_pdoa_mode);
    port_tx_msg (str, strlen (str));
    return 0;
}

/*
 * Queue a UWB data packet for transmission to another module.
 *   AT+UWBDATA=<dest>,<len>           then send <len> raw bytes on UART
 *   AT+UWBDATA=<dest>,<len>,<hex>     inline payload (small packets)
 */
int f_uwbdata (int opt, int argc, char* argv[])
{
    unsigned int dest;
    unsigned int len;

    if (argc == 2)
    {
        dest = (unsigned int) strtoul (argv[0], NULL, 10);
        len = (unsigned int) strtoul (argv[1], NULL, 10);
        if (len == 0U || len > UWB_DATA_MAX_PAYLOAD)
        {
            return -1;
        }
        if (uwb_data_uart_begin ((uint16_t) dest, (uint16_t) len) != 0)
        {
            return -1;
        }
        port_tx_msg ((uint8_t*) "\r\nOK\r\n", 6);
        return 0;
    }

    if (argc == 3)
    {
        uint8_t payload[UWB_DATA_MAX_PAYLOAD];

        dest = (unsigned int) strtoul (argv[0], NULL, 10);
        len = (unsigned int) strtoul (argv[1], NULL, 10);
        if (len == 0U || len > UWB_DATA_MAX_PAYLOAD)
        {
            return -1;
        }
        if (strlen (argv[2]) != (size_t) (len * 2U))
        {
            return -1;
        }
        if (uwb_data_hex_decode (argv[2], payload, (uint16_t) len) != 0)
        {
            return -1;
        }
        if (uwb_data_queue_tx ((uint16_t) dest, payload, (uint16_t) len) != 0)
        {
            return -1;
        }
        port_tx_msg ((uint8_t*) "\r\nOK\r\n", 6);
        return 0;
    }

    return -1;
}

//-----------------------------------------------------------------------------
/*
 * ????
 */
//-----------------------------------------------------------------------------
const command_t known_commands [] =
{
    /*TWR*/
    {"AT+GETVER", f_getver},
    {"AT+SAVE", f_save},
    {"AT+RESTART", f_restart},
    {"AT+UWBSTART", f_uwbstart},
    {"AT+RESTORE", f_restore},
    {"AT+GETCFG", f_getcfg},
    {"AT+SETCFG", f_setcfg},
    {"AT+GETDEV", f_getdev},
    {"AT+SETDEV", f_setdev},
    {"AT+GETWORKMODE", f_getworkmode},
    {"AT+SETWORKMODE", f_setworkmode},
    {"AT+GETSENSOR",   f_getsensor},
    {"AT+TESTLED",   f_testled},
    {"AT+TESTOLED",   f_testoled},
    {"AT+DISTANCE",   f_distance},
    /*PDOA*/
    {"AT+DECA$",   f_deca},
    {"AT+GETDLIST",   f_getdlist},
    {"AT+GETKLIST",   f_getklist},
    {"AT+ADDTAG",   f_addtag},
    {"AT+DELTAG",   f_deltag},
    {"AT+PDOAOFF",   f_pdoaoff},
    {"AT+RNGOFF",   f_rngoff},
    {"AT+FILTER",   f_filter},
    {"AT+PDOASETCFG",   f_pdoasetcfg},
    {"AT+PDOAGETCFG",   f_pdoagetcfg},
    {"AT+UARTRATE", f_uart_rate},
    {"AT+USER_CMD", f_user_cmd},
    /*????*/
    {"AT+GETUWBMODE", f_getewbmode},
    {"AT+SETUWBMODE", f_setuwbmode},
    {"AT+UWBDATA", f_uwbdata},

    {"AT", f_test},
    {NULL, NULL}
};