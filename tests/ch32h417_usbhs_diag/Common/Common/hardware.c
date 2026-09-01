/********************************** (C) COPYRIGHT *******************************
 * File Name          : hardware.c
 * Author             : WCH / T384 diagnostic adaptation
 * Description        : Petros project bring-up with USBHS-only diagnostic.
 *********************************************************************************
 * Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "hardware.h"
#include "ch32h417_swpmi.h"
#include "ch32h417_usbhs_device.h"

void Hardware(void)
{
    uint8_t enumeration_reported = 0;

    /* Keep the pin-release sequence from the proven Petros_DVP project. */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SWPMI, ENABLE);
    SWPMI_BypassCmd(ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    printf("Petros USBHS-only diagnostic start\r\n");
    USBHS_Device_Init(ENABLE);
    printf("USBHS attached; waiting for SET_CONFIGURATION\r\n");

    while (1)
    {
        if (USBHS_DevEnumStatus)
        {
            if (!enumeration_reported)
            {
                enumeration_reported = 1;
                printf("USBHS enumeration complete\r\n");
            }
        }
        else
        {
            enumeration_reported = 0;
        }
    }
}
