/**********************************************************************************
 * @file    user_out.c
 * @author Ai-Thinker
 * @version V1.0.0
 * @brief  distance and range output, Users can handle it themselves
 * @date    2024.9.18
**********************************************************************************/
#include "user_out.h"
#include "hal_usart.h"
#include "cmd_fn.h"
#include "config.h"
#include "uwb_pdoa.h"
#include <stdio.h>

extern float distance;

extern void pdoa2XY(result_pdoa_t *res, float pdoa_offset_deg);

bool aiio_output = false;
uint32_t twr_output_cb_count = 0;
uint16_t user_tag_dist_cm[MAX_ANCHOR_LIST_SIZE] = {0};

void set_aiio_output_state (bool output)
{
    aiio_output = output;
}

bool get_aiio_output_state (void)
{
    return aiio_output;
}

/* AT+RNGOFF stores rngOffset_mm in NVM but nothing previously consumed it -
 * apply it here so AT+DISTANCE and the Tag_Addr: stream both reflect the
 * calibrated range instead of the raw TWR measurement. */
static int user_out_apply_rng_offset_cm(int dist_cm)
{
    int16_t offset_mm = (app.pConfig != NULL) ? app.pConfig->s.s_pdoa.rngOffset_mm : 0;
    float corrected = (float) dist_cm + ((float) offset_mm / 10.0f);
    int corrected_cm = (int) (corrected + ((corrected >= 0.0f) ? 0.5f : -0.5f));
    return (corrected_cm < 0) ? 0 : corrected_cm;
}

static int16_t user_out_pick_pdoa_raw(void)
{
    if (uwb_poll_pdoa_raw != 0)
    {
        return uwb_poll_pdoa_raw;
    }
    if (uwb_range_pdoa_raw != 0)
    {
        return uwb_range_pdoa_raw;
    }
    if (uwb_final_pdoa != 0)
    {
        return uwb_final_pdoa;
    }

    return uwb_last_rx_pdoa_raw;
}

static void user_out_fill_pdoa_result(result_pdoa_t *out, uint16_t addr16, uint8_t range_num, int dist_cm)
{
    int16_t raw;
    int16_t angle_deg_x10;
    int x_cm;
    int y_cm;

    if (out == NULL || app.pConfig == NULL || dist_cm <= 0)
    {
        return;
    }

    raw = user_out_pick_pdoa_raw();
    memset(out, 0, sizeof(*out));
    out->addr16 = addr16;
    out->rangeNum = range_num;
    out->dist_cm = (float) user_out_apply_rng_offset_cm(dist_cm);

    angle_deg_x10 = uwb_pdoa_raw_to_deg_x10(raw);
    angle_deg_x10 = (int16_t) (angle_deg_x10 - (int16_t) (app.pConfig->s.s_pdoa.pdoaOffset_deg * 10));
    out->pdoa_raw_deg = (float) angle_deg_x10 / 10.0f;

    if (raw != 0)
    {
        out->angle = (float) angle_deg_x10 / 10.0f;
    }

    if (app.pConfig->s.s_pdoa.phaseCorrEn != 0U && raw != 0)
    {
        pdoa2XY(out, (float) app.pConfig->s.s_pdoa.pdoaOffset_deg);
        if (raw != 0)
        {
            out->angle = (float) angle_deg_x10 / 10.0f;
        }
    }

    if (out->x_cm == 0.0f && out->y_cm == 0.0f)
    {
        uwb_pdoa_polar_to_xy_cm((int) out->dist_cm, angle_deg_x10, &x_cm, &y_cm);
        out->x_cm = (float) x_cm;
        out->y_cm = (float) y_cm;
    }
}

static void node_emit_pdoa_from_twr (result_t *pRes)
{
    result_pdoa_t out;

    if (pRes == NULL || app.pConfig == NULL)
    {
        return;
    }
    if (app.pConfig->s.s_pdoa.user_cmd == 0)
    {
        return;
    }
    if (pRes->dist_cm[0] == 0U)
    {
        return;
    }

    user_out_fill_pdoa_result(&out, pRes->addr16, pRes->rangeNum, (int) pRes->dist_cm[0]);
    node_pdoa_output_user(&out);
}

void user_out_stream_pdoa_on_anchor_tick (void)
{
    static int last_dist_mm = -1;
    static int16_t last_pdoa_raw = 0;
    result_pdoa_t out;
    int dist_mm;
    int16_t raw;

    if (app.pConfig == NULL || app.pConfig->s.userConfig.role != 1)
    {
        return;
    }
    if (app.pConfig->s.s_pdoa.user_cmd == 0)
    {
        return;
    }

    dist_mm = (int) (distance * 1000.0f + 0.5f);
    raw = user_out_pick_pdoa_raw();
    if (dist_mm <= 0)
    {
        return;
    }
    if (dist_mm == last_dist_mm && raw == last_pdoa_raw)
    {
        return;
    }
    last_dist_mm = dist_mm;
    last_pdoa_raw = raw;

    user_out_fill_pdoa_result(&out,
                              app.pConfig->s.userConfig.nodeAddr,
                              0,
                              dist_mm);
    node_pdoa_output_user(&out);
}

void node_twr_output_user (result_t* pRes)
{
    if (pRes == NULL)
    {
        return;
    }

    twr_output_cb_count++;
    distance = pRes->dist_cm[0] / 100.0f;
    f_stream_distance();
    node_emit_pdoa_from_twr(pRes);
}

void tag_twr_output_user (uint16_t* dis)
{
    if (dis == NULL)
    {
        return;
    }

    twr_output_cb_count++;
    distance = dis[0] / 100.0f;
    f_stream_distance();
}

void node_pdoa_output_user (result_pdoa_t* Res)
{
    if (Res == NULL)
    {
        return;
    }

    char buf[160];
    int dist_mm = (int) (Res->dist_cm * 10.0f + 0.5f);
    int range_whole = dist_mm / 1000;
    int range_frac = dist_mm % 1000;
    int angle_deg_x10 = (int) (Res->angle * 10.0f + ((Res->angle >= 0.0f) ? 0.5f : -0.5f));
    int len = sprintf(buf,
                      "Tag_Addr:%04X, Seq:%d, Xcm:%d, Ycm:%d, Range:%d.%03d, Angle:%d\r\n",
                      Res->addr16,
                      (int) Res->rangeNum,
                      (int) (Res->x_cm + ((Res->x_cm >= 0.0f) ? 0.5f : -0.5f)),
                      (int) (Res->y_cm + 0.5f),
                      range_whole,
                      range_frac,
                      angle_deg_x10);
    USART1_SendBuffer(buf, (uint16_t) len, 1);
}

void tag_pdoa_output_user (result_pdoa_t* Res)
{
    if (Res == NULL)
    {
        return;
    }

    char buf[128];
    int dist_mm = (int) (Res->dist_cm * 10.0f + 0.5f);
    int range_whole = dist_mm / 1000;
    int range_frac = dist_mm % 1000;
    int angle_deg_x10 = (int) (Res->angle * 10.0f + ((Res->angle >= 0.0f) ? 0.5f : -0.5f));
    int len = sprintf(buf,
                      "Tag_Addr:%04X, Seq:%d, Xcm:%d, Ycm:%d, Range:%d.%03d, Angle:%d\r\n",
                      Res->addr16,
                      (int) Res->rangeNum,
                      (int) (Res->x_cm + ((Res->x_cm >= 0.0f) ? 0.5f : -0.5f)),
                      (int) (Res->y_cm + 0.5f),
                      range_whole,
                      range_frac,
                      angle_deg_x10);
    USART1_SendBuffer(buf, (uint16_t) len, 1);
}
