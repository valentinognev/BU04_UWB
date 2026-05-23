/**********************************************************************************
 * @file    main.c
 * @author Ai-Thinker
 * @version V1.0.0
 * @date    2024.5.16
**********************************************************************************/

#include "stm32f10x.h"
#include "Generic.h"
#include "node.h"
#include "tag.h"
#include "OSAL.h"
#include "hal_drivers.h"

#include "config.h"
#include "hal_iic.h"
#include "cmd_fn.h"
#include "OLED_I2C.h"

#include "shared_functions.h"
#include "user_out.h"
#include "examples_defines.h"
#include "deca_device_api.h"
#include "uwb_pdoa.h"

extern void tag_start(void);
extern volatile uint8_t uwb_ranging_active;  /* defined in stm32f10x_it.c */
extern volatile uint8_t tag_bootstrap_pending; /* defined in tag_bootstrap.c */

/*******************************************************************************
*******************************************************************************/

void RCC_Configuration_part (void)
{
    ErrorStatus HSEStartUpStatus;
    RCC_ClocksTypeDef RCC_ClockFreq;

    /* ??RCC?????????????????? */
    RCC_DeInit();

    /* ??????????????HSE */
    RCC_HSEConfig (RCC_HSE_ON);

    /* ????????????????? */
    HSEStartUpStatus = RCC_WaitForHSEStartUp();

    if (HSEStartUpStatus != ERROR)
    {
        /* ????Flash??????????,??????????? */
        FLASH_PrefetchBufferCmd (FLASH_PrefetchBuffer_Enable);

        /* 48~72Mhz???Latency?2 */
        FLASH_SetLatency (FLASH_Latency_2);

        /* ????AHB????72MHz HCLK = SYSCLK */
        RCC_HCLKConfig (RCC_SYSCLK_Div1);
        /* ???????APB2????1???72MHz PCLK2 = HCLK */
        RCC_PCLK2Config (RCC_HCLK_Div1);
        /* ???????APB1????2???36MHz PCLK1 = HCLK/2 */
        RCC_PCLK1Config (RCC_HCLK_Div2);
        /*  ????ADC??? ADCCLK = PCLK2/4 */
        RCC_ADCCLKConfig (RCC_PCLK2_Div6);

        //????PLL???????????? ???????RCC_PLLSource_HSE_Div1 9?????RCC_PLLMul_9
        RCC_PLLConfig (RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
        /* ??PLL */
        RCC_PLLCmd (ENABLE);
        /* ???PLL??????? */
        while (RCC_GetFlagStatus (RCC_FLAG_PLLRDY) == RESET) {}

        /* ???PLL?????????? */
        RCC_SYSCLKConfig (RCC_SYSCLKSource_PLLCLK);

        /* ?????????????????????? */
        while (RCC_GetSYSCLKSource() != 0x08) {}
    }

    RCC_GetClocksFreq (&RCC_ClockFreq);
}

/*******************************************************************************
* ??????  : init
* ????    : ?????????
* ????    : ??
* ???    : ??
* ?????  : ??
*******************************************************************************/
void init (void)
{
    SystemInit();

    RCC_Configuration_part();

    Hal_Driver_Init();

    App_Module_Init();
}
/*******************************************************************************
* ??????  : nt_task
* ????    : main task
* ????    : ??
* ???    : ??
* ?????  : ??
*******************************************************************************/
void nt_task (void)
{
    //???flash
    load_bssConfig();
    set_aiio_output_state (false);

    bool pending_uwb = (sys_para.param_Config.s.userConfig.workmode == 2);
    if (pending_uwb)
    {
        sys_para.param_Config.s.userConfig.workmode = 0;
        App_Module_Sys_Write_NVM();
    }

    if (sys_para.param_Config.s.userConfig.workmode == 0)
    {
        OLED_ShowStr (19, 2, "Please Send", 2);
        OLED_ShowStr (19, 4, "AT Command", 2);

        while (app.pConfig->s.userConfig.nodeAddr == 0xFFFF) //AOA?????????
        {
            App_Module_Sys_Work_Mode_Event();
        }

        /* Stay in AT command mode on normal boots. UWB starts only once after AT+SAVE
         * (workmode sentinel 2 written to flash by f_save). */
        if (!pending_uwb)
        {
            while (1)
            {
                App_Module_Sys_Work_Mode_Event();
            }
        }

        if (app.pConfig->s.userConfig.twr_pdoa_mode == 0)
        {
            if (sys_para.flag == 0xAAAA)
            {
                set_aiio_output_state (true);
                uwb_pdoa_prepare_config(&sys_para.param_Config);
                if (app.pConfig != NULL)
                {
                    uwb_pdoa_prepare_config(app.pConfig);
                }
                if (sys_para.param_Config.s.userConfig.role == 1) //node
                {
                    uwb_ranging_active = 1;
                    node_start();
                }
                else if (sys_para.param_Config.s.userConfig.role == 0) //tag
                {
                    uwb_ranging_active = 1;
                    tag_bootstrap_pending = 1;
                    tag_start();
                }
            }
        }
        else
        {
            if (sys_para.param_Config.s.userConfig.role == 1)
            {
                ds_twr_sts_sdc_responder();
            }
            else if (sys_para.param_Config.s.userConfig.role == 0)
            {
                ds_twr_sts_sdc_initiator();
            }
        }
    }
    else
    {
        while (1)
        {
            App_Module_Sys_Work_Mode_Event();
        }
    }

    for (;;);
}

/*******************************************************************************
* ??????  : main
* ????    : ??????
* ????    : ??
* ???    : ??
* ?????  : ??
********************************************************************************/
int main (void)
{
#define DUMMY_HI_Z 0
#if DUMMY_HI_Z
    /* Configure all UWB interface pins as Input Floating to release bus to FTDI */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitTypeDef g;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    
    /* PA0 (RSTN), PA4 (CS), PA5 (CLK), PA6 (MISO), PA7 (MOSI) */
    g.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOA, &g);
    
    /* PB0 (WAKEUP), PB5 (IRQ) */
    g.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_5;
    GPIO_Init(GPIOB, &g);
    
    /* Loop forever in High-Z state */
    while (1) {
        __WFI();
    }
#endif

    init();
#if(EXAMPLE_DEMO)
    build_examples();
#else
		nt_task();
#endif
    for (;;)
    {
    }
}

