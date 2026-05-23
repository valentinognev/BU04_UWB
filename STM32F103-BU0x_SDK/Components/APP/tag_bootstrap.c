#include <stdint.h>
#include "default_config.h"

extern param_block_t *get_pbssConfig(void);
extern void *getTwrInfoPtr_tag(void);

volatile uint8_t tag_bootstrap_pending = 0;
volatile uint32_t tag_bootstrap_count = 0;

#define TAG_OFF_STATE       0
#define TAG_OFF_SLOT_ACC    0x1d4
#define TAG_OFF_SLOT_MS     0x1c8
#define TAG_OFF_SLOT_NEXT   0x234
#define TAG_OFF_SLOT_BASE   0x23c
#define TAG_OFF_SLOT_DEAD   0x238
#define TAG_OFF_POLL_GATE   0x231
#define TAG_OFF_SLOT_ARMED  0x232

#define TAG_OFF_FINAL_DLY   0x1cc
#define TAG_OFF_POLL_DEST   0x1ce

void tag_bootstrap_slot_scheduler(void)
{
    uint8_t *tag = (uint8_t *) getTwrInfoPtr_tag();
    param_block_t *cfg = get_pbssConfig();
    uint32_t acc;
    uint32_t base;
    uint16_t slot_ms;

    if (tag == NULL || cfg == NULL)
    {
        return;
    }

    slot_ms = *(uint16_t *) (tag + TAG_OFF_SLOT_MS);
    if (slot_ms == 0U)
    {
        slot_ms = cfg->s.sfConfig.slotPeriod;
    }
    if (slot_ms == 0U)
    {
        slot_ms = DEFAULT_SLOT_PERIOD_MS;
    }

    acc = *(uint32_t *) (tag + TAG_OFF_SLOT_ACC);
    base = *(uint32_t *) (tag + TAG_OFF_SLOT_BASE);
    *(uint32_t *) (tag + TAG_OFF_SLOT_NEXT) = acc + (uint32_t) slot_ms;
    *(uint32_t *) (tag + TAG_OFF_SLOT_DEAD) = base + (uint32_t) slot_ms;
    tag[TAG_OFF_SLOT_ARMED] = 1U;
    *(uint32_t *) (tag + TAG_OFF_SLOT_ACC) = 0U;
    if (cfg->s.s_pdoa.addr != 65535U)
    {
        *(uint16_t *) (tag + TAG_OFF_POLL_DEST) = cfg->s.s_pdoa.addr;
    }
    tag_bootstrap_count++;
}

void tag_sync_poll_dest(void)
{
    uint8_t *tag = (uint8_t *) getTwrInfoPtr_tag();
    param_block_t *cfg = get_pbssConfig();

    if (tag == NULL || cfg == NULL)
    {
        return;
    }
    if (cfg->s.s_pdoa.addr != 65535U)
    {
        *(uint16_t *) (tag + TAG_OFF_POLL_DEST) = cfg->s.s_pdoa.addr;
    }
}

void tag_sync_twr_timing(void)
{
    uint8_t *tag = (uint8_t *) getTwrInfoPtr_tag();
    param_block_t *cfg = get_pbssConfig();

    if (tag == NULL || cfg == NULL)
    {
        return;
    }

    *(uint16_t *) (tag + TAG_OFF_FINAL_DLY) = cfg->s.sfConfig.tag_pollTxFinalTx_us;

    if (tag[TAG_OFF_SLOT_ARMED] == 0U)
    {
        tag_bootstrap_slot_scheduler();
    }
}

void tag_bootstrap_tick(void)
{
    if (tag_bootstrap_pending)
    {
        tag_bootstrap_pending = 0;
        tag_bootstrap_slot_scheduler();
    }
}

uint8_t tag_read_resp_flag(void)
{
    uint8_t *tag = (uint8_t *) getTwrInfoPtr_tag();

    return (tag != NULL) ? tag[0x1d2] : 0U;
}

void tag_read_scheduler_diag(uint8_t *state, uint8_t *poll_gate, uint8_t *slot_armed)
{
    uint8_t *tag = (uint8_t *) getTwrInfoPtr_tag();

    if (state != NULL)
    {
        *state = (tag != NULL) ? tag[TAG_OFF_STATE] : 0U;
    }
    if (poll_gate != NULL)
    {
        *poll_gate = (tag != NULL) ? tag[TAG_OFF_POLL_GATE] : 0U;
    }
    if (slot_armed != NULL)
    {
        *slot_armed = (tag != NULL) ? tag[TAG_OFF_SLOT_ARMED] : 0U;
    }
}
