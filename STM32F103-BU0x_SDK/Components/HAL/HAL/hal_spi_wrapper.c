/**
 * @file    hal_spi_wrapper.c
 * @brief   SPI transaction wrappers that fix SSM (software NSS) before each transfer.
 *
 * Root cause: The precompiled uwb.o SPI_ConfigFastRate() builds SPI_InitStructure
 * with SPI_NSS = SPI_NSS_Hard (SSM=0). With SSM=0, when we pull CS/PA4 LOW to
 * assert chip-select, STM32 SPI1 reads NSS=LOW and silently switches to slave mode,
 * disabling the master clock. All subsequent reads return 0xFF (MISO idle HIGH).
 *
 * Fix: Override readfromspi_serial / writetospi_serial by renaming the precompiled
 * versions to *_hw and wrapping them here. Set SPI1_CR1 SSM=1, SSI=1 before each
 * call so SPI1 stays in software-NSS master mode.
 */

#include "stm32f10x.h"
#include "hal_spi.h"
#include <stdint.h>

extern volatile uint8_t uwb_ranging_active;

static uint8_t uwb_spi_fast_mode = 0;

/* Forward-declare the renamed precompiled implementations from uwb.o */
extern int writetospi_serial_hw(uint16_t headerLength, const uint8_t *headerBuffer,
                                uint32_t bodylength, const uint8_t *bodyBuffer);
extern int readfromspi_serial_hw(uint16_t headerLength, const uint8_t *headerBuffer,
                                 uint32_t readlength, uint8_t *readBuffer);

/* SPI1 base address and CR1 register.
 * SPI_CR1 bit 9 = SSM (software slave management)
 * SPI_CR1 bit 8 = SSI (internal slave select, must be 1 in SW-NSS master mode)
 */
#define SPI1_CR1  (*(volatile uint16_t *)0x40013000U)
#define SPI_CR1_SSM  (1U << 9)
#define SPI_CR1_SSI  (1U << 8)

static inline void spi1_fix_ssm(void)
{
    /* Ensure SPI1 stays in master mode regardless of CS/PA4 level.
     * Must be set each time SPI is reconfigured by the precompiled lib. */
    SPI1_CR1 |= (SPI_CR1_SSM | SPI_CR1_SSI);
}

static void spi1_reinit(uint16_t prescaler)
{
    SPI_InitTypeDef SPI_InitStructure;

    SPI_I2S_DeInit(SPI1);
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = prescaler;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);
    GPIO_SetBits(DW1000_CS_PORT, DW1000_CS_PIN);
    spi1_fix_ssm();
}

extern uint16_t USART1_SendBuffer (const char* buffer, uint16_t length, int flag);

void __wrap_port_set_dw_ic_spi_slowrate(void)
{
    USART1_SendBuffer("SPI_SLOW\r\n", 10, 1);
    uwb_spi_fast_mode = 0;
    /* ≤7 MHz required during INIT_RC; /32 ≈ 2.25 MHz at 72 MHz APB2. */
    spi1_reinit(SPI_BaudRatePrescaler_32);
}

void __wrap_port_set_dw_ic_spi_fastrate(void)
{
    USART1_SendBuffer("SPI_FAST\r\n", 10, 1);
    uwb_spi_fast_mode = 1;
    /* Runtime TWR timestamp reads; /8 ≈ 9 MHz post-init. */
    spi1_reinit(SPI_BaudRatePrescaler_8);
}

int writetospi_serial(uint16_t headerLength, const uint8_t *headerBuffer,
                      uint32_t bodylength, const uint8_t *bodyBuffer)
{
    spi1_fix_ssm();
    return writetospi_serial_hw(headerLength, headerBuffer, bodylength, bodyBuffer);
}

int readfromspi_serial(uint16_t headerLength, const uint8_t *headerBuffer,
                       uint32_t readlength, uint8_t *readBuffer)
{
    spi1_fix_ssm();
    return readfromspi_serial_hw(headerLength, headerBuffer, readlength, readBuffer);
}

void reset_DWIC(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /* Drive PA0 (RSTn) low */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_ResetBits(GPIOA, GPIO_Pin_0);

    /* Keep reset low for 10 ms (original was 2 ms) */
    extern void sleep_ms(uint32_t ms);
    sleep_ms(10);

    /* Configure PA0 back to Input Pull-up */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_SetBits(GPIOA, GPIO_Pin_0); /* Enable pull-up */

    /* Wait 50 ms for DW3000 to boot up */
    sleep_ms(50);
}

void setup_DW1000RSTnIRQ(int enable)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    if (enable)
    {
        /* Configure PA0 (RSTn) as Input Pull-Up */
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
        GPIO_Init(GPIOA, &GPIO_InitStructure);
        GPIO_SetBits(GPIOA, GPIO_Pin_0); /* Enable pull-up */

        /* EXTI Line0 Configuration */
        GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);

        EXTI_InitStructure.EXTI_Line = EXTI_Line0;
        EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
        EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
        EXTI_InitStructure.EXTI_LineCmd = ENABLE;
        EXTI_Init(&EXTI_InitStructure);

        /* NVIC Configuration */
        NVIC_PriorityGroupConfig(NVIC_PriorityGroup_3);

        NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 15;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);
    }
    else
    {
        /* Configure PA0 as Analog Input (AIN) to disable it */
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOA, &GPIO_InitStructure);
    }
}
