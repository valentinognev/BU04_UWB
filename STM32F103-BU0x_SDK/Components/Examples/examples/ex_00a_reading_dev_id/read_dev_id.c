/*! ----------------------------------------------------------------------------
 *  @file    read_dev_id.c
 *  @brief   This example just read DW IC's device ID. It can be used to verify
 *           the SPI comms are working correctly.
 *
 * @attention
 *
 * Copyright 2018-2020 (c) Decawave Ltd, Dublin, Ireland.
 *
 * All rights reserved.
 *
 * @author Decawave
 */

#include <stdio.h>
#include <deca_device_api.h>
#include <example_selection.h>
#include "uwb.h"

#if defined(TEST_READING_DEV_ID)

/* Example application name and version to display on LCD screen/VCOM port. */
#define APP_NAME "READ DEV ID      "

/**
 * Application entry point.
 */
int read_dev_id(void)
{
    int err;
    /* �������Ӧ������. (Display application name on LCD.) */
    _dbg_printf((unsigned char *)APP_NAME);

    /* ����SPI������. (Configure SPI rate, DW3000 supports up to 38 MHz) */
    port_set_dw_ic_spi_fastrate();

    /* Ӳ��λDW3000ģ�� (Reset DW IC) */
    reset_DWIC(); /* Target specific drive of RSTn line into DW IC low for a period. */

    Sleep(2); // Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC, or could wait for SPIRDY event)

    /* ��ȡDW3000ģ��deviceID. (Reads and validate device ID returns DWT_ERROR if it does not match expected else DWT_SUCCESS) */
    if ((err=dwt_check_dev_id())==DWT_SUCCESS)
    {
        _dbg_printf((unsigned char *)"DEV ID OK");
    }
    else
    {
        _dbg_printf((unsigned char *)"DEV ID FAILED");
    }

    return err;
}

#endif
/*****************************************************************************************************************************************************
 * NOTES:
 ****************************************************************************************************************************************************/
