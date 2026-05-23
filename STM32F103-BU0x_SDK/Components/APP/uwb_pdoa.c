#include "uwb_pdoa.h"
#include "deca_device_api.h"
#include "config.h"
#include <math.h>
#include <string.h>

volatile int16_t uwb_range_pdoa_raw = 0;
volatile int16_t uwb_poll_pdoa_raw = 0;
uint8_t uwb_pdoa_lna_enable = 0;

void uwb_pdoa_prepare_config(param_block_t *pcfg)
{
    if (pcfg == NULL)
    {
        return;
    }

    /* DW3000 M3 PDOA needs STS on both sides; vendor TWR stack reads sts quality. */
    pcfg->dwt_config.pdoaMode = DWT_PDOA_M3;
    pcfg->dwt_config.stsMode = (uint8_t) (DWT_STS_MODE_1 | DWT_STS_MODE_SDC);
    pcfg->dwt_config.stsLength = DWT_STS_LEN_64;
    pcfg->s.s_pdoa.phaseCorrEn = 1U;
    pcfg->s.sfConfig.tag_replyDly_us = DEFAULT_TAG_POLL_TX_RESPONSE_RX_US;
    pcfg->s.s_pdoa.sfConfig_pdoa.tag_replyDly_us = DEFAULT_TAG_POLL_TX_RESPONSE_RX_US;
    uwb_pdoa_lna_enable = 1U;
}

void uwb_pdoa_apply_after_start(uint8_t is_anchor)
{
    dwt_config_t cfg;

    if (uwb_pdoa_lna_enable == 0U || app.pConfig == NULL)
    {
        return;
    }

    memcpy(&cfg, &app.pConfig->dwt_config, sizeof(cfg));
    cfg.pdoaMode = DWT_PDOA_M3;
    cfg.stsMode = (uint8_t) (DWT_STS_MODE_1 | DWT_STS_MODE_SDC);
    cfg.stsLength = DWT_STS_LEN_64;
    if (dwt_configure(&cfg) == DWT_SUCCESS)
    {
        dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);
        if (is_anchor != 0U)
        {
            dwt_setlnapamode(DWT_LNA_ENABLE);
        }
    }
}

int16_t uwb_pdoa_raw_to_deg_x10(int16_t raw)
{
    float       deg;
    int32_t     deg_x10;

    deg = ((float) raw / 2048.0f) * 180.0f / (float) M_PI;
    if (deg >= 0.0f)
    {
        deg_x10 = (int32_t) (deg * 10.0f + 0.5f);
    }
    else
    {
        deg_x10 = (int32_t) (deg * 10.0f - 0.5f);
    }

    while (deg_x10 > 1800)
    {
        deg_x10 -= 3600;
    }
    while (deg_x10 < -1800)
    {
        deg_x10 += 3600;
    }

    return (int16_t) deg_x10;
}

void uwb_pdoa_polar_to_xy_cm(int dist_cm, int angle_deg_x10, int *x_cm, int *y_cm)
{
    int64_t a = angle_deg_x10;
    int64_t d = dist_cm;

    if (x_cm != NULL)
    {
        *x_cm = (int) ((d * a + 573) / 1146);
    }
    if (y_cm != NULL)
    {
        int64_t aa = (a * a) / 10;
        *y_cm = (int) ((d * (1000 - aa / 2000)) / 1000);
        if (*y_cm < 0)
        {
            *y_cm = 0;
        }
    }
}
