#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_KNOWN_TAG_LIST_SIZE (19)
#define MAX_DISCOVERED_TAG_LIST_SIZE (10)

typedef struct __attribute__((packed))
{
    uint8_t chan ;           //!< Channel number (5 or 9)
    uint8_t txPreambLength ; //!< DWT_PLEN_64..DWT_PLEN_4096
    uint8_t rxPAC ;          //!< Acquisition Chunk Size (Relates to RX preamble length)
    uint8_t txCode ;         //!< TX preamble code (the code configures the PRF, e.g. 9 -> PRF of 64 MHz)
    uint8_t rxCode ;         //!< RX preamble code (the code configures the PRF, e.g. 9 -> PRF of 64 MHz)
    uint8_t sfdType;         //!< SFD type (0 for short IEEE 8-bit standard, 1 for DW 8-bit, 2 for DW 16-bit, 3 for 4z BPRF)
    uint8_t dataRate ;       //!< Data rate {DWT_BR_850K or DWT_BR_6M8}
    uint8_t phrMode ;        //!< PHR mode {0x0 - standard DWT_PHRMODE_STD, 0x3 - extended frames DWT_PHRMODE_EXT}
    uint8_t phrRate;         //!< PHR rate {0x0 - standard DWT_PHRRATE_STD, 0x1 - at datarate DWT_PHRRATE_DTA}
    uint16_t sfdTO ;         //!< SFD timeout value (in symbols)
    uint8_t stsMode;         //!< STS mode (no STS, STS before PHR or STS after data)
    uint32_t stsLength;      // enum
    uint8_t pdoaMode;        //!< PDOA mode
} dwt_config_t ;

typedef struct __attribute__((packed))
{
	uint16_t    antRx;						//
	uint16_t    antTx;						//
}baseConfig_t;

typedef struct __attribute__((packed))
{
	uint16_t    slotPeriod;					//
	uint16_t    numSlots;					//
	uint16_t    sfPeriod_ms;				//
	uint16_t    tag_replyDly_us;			//
	uint16_t    tag_pollTxFinalTx_us;		//
}sfConfig_t;

typedef struct __attribute__((packed))
{
	uint16_t    nodeAddr;					//
	uint8_t     role;             //
	uint8_t     workmode;             //
	uint8_t     twr_pdoa_mode;      //
	uint16_t    nodePanid;					//
	uint16_t    nodeFilter;					//
	uint16_t    tagnumSlots;				//
	float       kalman_Q;					//
	float       kalman_R;					//
	float       para_a;						//
	float       para_b;						//
	uint16_t    Pa_mod;						//
}userConfig_t;

typedef struct {
    uint16_t    slotPeriod;
    uint16_t    numSlots;
    uint16_t    sfPeriod_ms;
    uint16_t    tag_replyDly_us;
    uint16_t    tag_pollTxFinalTx_us;
    uint16_t    tag_mFast;
    uint16_t    tag_mSlow;
    uint16_t    tag_Mode;
} sfConfig_t_pdoa;

typedef struct {
    uint16_t    addr;
    uint16_t    panID;
    int16_t     pdoaOffset_deg;
    int16_t     rngOffset_mm;
    int16_t     pdoa_temp_coeff_mrad;
    uint8_t     rbcEn;
    uint8_t     phaseCorrEn;
    uint8_t     smartTxEn;
    uint8_t     reportLevel;
    uint16_t    acc_stationary_ms;
    uint16_t    acc_moving_ms;
    uint16_t    acc_threshold;
    uint16_t    rcDelay_us;
    uint16_t    motionfilter;
    uint16_t    user_cmd;
    uint8_t     Dlist;
    uint8_t     Klist;
    uint8_t     UartReFreshRate;
    uint8_t     reserver;
    uint16_t    antRx_a;
    uint16_t    antTx_a;
    uint16_t    antRx_b;
    uint16_t    antTx_b;
    sfConfig_t_pdoa  sfConfig_pdoa;
}__attribute__((packed)) run_pdoa_t;

typedef struct 
{       
	baseConfig_t baseConfig;
	sfConfig_t   sfConfig;
	userConfig_t userConfig;
	run_pdoa_t    s_pdoa;
}__attribute__((packed)) run_t;

typedef struct 
{
	run_t           s;
	dwt_config_t    dwt_config;
}__attribute__((packed)) param_block_t;

int main() {
    printf("sizeof(baseConfig_t): %zu\n", sizeof(baseConfig_t));
    printf("sizeof(sfConfig_t): %zu\n", sizeof(sfConfig_t));
    printf("sizeof(userConfig_t): %zu\n", sizeof(userConfig_t));
    printf("sizeof(sfConfig_t_pdoa): %zu\n", sizeof(sfConfig_t_pdoa));
    printf("sizeof(run_pdoa_t): %zu\n", sizeof(run_pdoa_t));
    printf("sizeof(run_t): %zu\n", sizeof(run_t));
    printf("offsetof(param_block_t, dwt_config): %zu (0x%zx)\n", offsetof(param_block_t, dwt_config), offsetof(param_block_t, dwt_config));
    return 0;
}
