#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "hal_usart.h"
#include "Generic_cmd.h"

#include <string.h>
#include "hal_usart.h"
#include "Generic_cmd.h"

extern volatile uint8_t uwb_ranging_active;

static int dbg_is_twr_fault(const char *format)
{
    return (strstr(format, "fault") != NULL)
        || (strstr(format, "error") != NULL)
        || (strstr(format, "fail") != NULL)
        || (strstr(format, "poll") != NULL)
        || (strstr(format, "final") != NULL)
        || (strstr(format, "resp") != NULL)
        || (strstr(format, "recv") != NULL)
        || (strstr(format, "send") != NULL)
        || (strstr(format, "msg") != NULL);
}

#define SRC_USART1_DR   (&(USART1->DR))     //???????????


#define DMA1_MEM_LEN 256//????DMA??????????????
char _dbg_TXBuff[DMA1_MEM_LEN];

char USART1_DMA_RX_BYTE;

/*
 *******************************************************************************
 *   ????1???????
 *   ???????????????,?????????????,????????
 *   ????????????????????
 *******************************************************************************
 */
uint16_t USART1_SendBuffer (const char* buffer, uint16_t length, int flag)
{
    (void) flag;
#ifdef HAL_USART1_DMA
    DMA_Cmd (DMA1_Channel4, DISABLE);
    USART_DMACmd (USART1, USART_DMAReq_Tx, DISABLE);
#endif
    for (int i = 0; i < length; i++)
    {
        USART1->SR;
        USART_SendData (USART1, (uint8_t) buffer[i]);
        while (USART_GetFlagStatus (USART1, USART_FLAG_TC) == RESET);
    }
    return length;
}

/*
 *******************************************************************************
 *      DMA?????_dbg_printf
 *******************************************************************************
 */
void _dbg_printf (const char* format, ...)
{
#ifdef HW_RELEASE
#else
    if (uwb_ranging_active)
    {
        return;
    }

    uint32_t length;
    va_list args;

    va_start (args, format);
    length = vsnprintf ( (char*) _dbg_TXBuff, sizeof (_dbg_TXBuff), (char*) format, args);
    va_end (args);

    USART1_SendBuffer ( (const char*) _dbg_TXBuff, length, true);
#endif
}
#define printf _dbg_printf
/*
 *******************************************************************************
 *      ????1DMA?????
 *      ???RXNE??TC????,????IDLE
 *      ????RX,TX????????????????
 *******************************************************************************
 */
void HalUASRT1_DMA_Config (void)
{
    DMA_InitTypeDef DMA_InitStructure;

    RCC_AHBPeriphClockCmd (RCC_AHBPeriph_DMA1, ENABLE);

    /* USART1 TX: polled only (FTDI/AT). TX DMA caused garbled serial output. */

    //RX????DMA
    DMA_DeInit (DMA1_Channel5);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32) SRC_USART1_DR;                  //DMA????????
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32) &USART1_DMA_RX_BYTE;                    //DMA???????
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                       //??????????????????
    DMA_InitStructure.DMA_BufferSize = 1;                                    //DMA???????
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;         //???????????????
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                  //??????????????
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;//???????????8bit
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;          //??????????8bit
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;                          //?????
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;                          //?????????
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;                                 //?????????
    DMA_Init (DMA1_Channel5, &DMA_InitStructure);                            //?????DMA

    DMA_ITConfig (DMA1_Channel5, DMA_IT_TC, ENABLE); //???DMA???6???????????
    DMA_Cmd (DMA1_Channel5, ENABLE);                                //???DMA???6
    USART_DMACmd (USART1, USART_DMAReq_Rx, ENABLE); //???USART2????DMA????
}

void HalUSART1_DMA_TX_NVIC_Config (void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 10;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init (&NVIC_InitStructure);
}
void HalUSART1_DMA_RX_NVIC_Config (void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 10;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init (&NVIC_InitStructure);
}


void HalUASRT1_NVIC_Config (void)
{

}


void HalUSART1_Init (u32 bound)
{
    USART_InitTypeDef USART_InitStructure;

    //USART ?????????

    USART_InitStructure.USART_BaudRate = bound;//?????????
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;//????8????????
    USART_InitStructure.USART_StopBits = USART_StopBits_1;//???????
    USART_InitStructure.USART_Parity = USART_Parity_No;//???????????
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//???????????????
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; //?????

    USART_Init (USART1, &USART_InitStructure); //?????????
    USART_ITConfig (USART1, USART_IT_RXNE, ENABLE); //???????????????
    USART_Cmd (USART1, ENABLE);                   //??????
}

void HalUSART1_IO_Init (void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd (RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE); //???USART1??GPIOA???
    //USART1_TX   GPIOA.9
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; //PA.9
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; //???????????
    GPIO_Init (GPIOA, &GPIO_InitStructure); //?????GPIOA.9

    //USART1_RX   GPIOA.10?????
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;//PA10
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//????????
    GPIO_Init (GPIOA, &GPIO_InitStructure); //?????GPIOA.10
}

void HalUSART2_Init (void)
{
    GPIO_InitTypeDef            GPIO_InitStructure;
    USART_InitTypeDef           USART_InitStructure;  //???????????????
    NVIC_InitTypeDef            NVIC_InitStructure;

    RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd (RCC_APB1Periph_USART2, ENABLE);


    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;          //????????????????
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;  //??????????????
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;//??????9????
    GPIO_Init (GPIOA, &GPIO_InitStructure);          //??????????????????????????????

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init (GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200; //??????
    USART_InitStructure.USART_WordLength = USART_WordLength_8b; //??????8??
    USART_InitStructure.USART_StopBits = USART_StopBits_1;  //????1??
    USART_InitStructure.USART_Parity = USART_Parity_No;     //?????? ??
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//????????
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;     //??????????????

    USART_Init (USART2, &USART_InitStructure); //?????????????????????USART_Init?????????
    USART_Cmd (USART2, ENABLE); //????USART2

    USART_ITConfig (USART2, USART_IT_RXNE, ENABLE);

    USART_ClearFlag (USART2, USART_FLAG_TC);
    NVIC_PriorityGroupConfig (NVIC_PriorityGroup_1);
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init (&NVIC_InitStructure);

}

void USART2SendByteData (unsigned char c)
{
    USART_SendData (USART2, c);
    while (USART_GetFlagStatus (USART2, USART_FLAG_TXE) == RESET);
}


void USART2SendDatas (unsigned char* str, char len)
{
    for (char n = 0; n < len; n++)
    {
        USART_SendData (USART2, str[n]);
        while (USART_GetFlagStatus (USART2, USART_FLAG_TXE) == RESET);

    }
}
#if 1
void USART2_IRQHandler (void)
{
    if (USART_GetITStatus (USART2, USART_IT_RXNE) != RESET)
    {
        uint8_t buff = USART_ReceiveData (USART2);
        App_Module_USART2_RxByte (buff);
        USART_ClearITPendingBit (USART2, USART_IT_RXNE);
    }
}
#endif

void HalUARTInit (void)
{
#ifdef HAL_USART1
    HalUSART1_IO_Init();
    HalUSART1_Init (115200);
#ifdef HAL_USART1_DMA
    HalUASRT1_DMA_Config();
    HalUSART1_DMA_RX_NVIC_Config();
#endif
#endif
#ifdef HAL_USART2
    HalUSART2_Init();
#endif

}


