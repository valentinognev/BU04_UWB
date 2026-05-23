#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define __packed __attribute__((packed))

#include "/home/valentin/RL/UWB/Refs/STM32F103-BU0x_SDK/Components/HAL/DW/decadriver/deca_device_api.h"
#include "/home/valentin/RL/UWB/Refs/STM32F103-BU0x_SDK/Components/HAL/DW/twr_pdoa/inc/default_config.h"

int main() {
    printf("sizeof(tag_addr_slot_t): %zu\n", sizeof(tag_addr_slot_t));
    printf("offsetof(param_block_t, knownTagList): %zu\n", offsetof(param_block_t, knownTagList));
    printf("sizeof(param_block_t): %zu\n", sizeof(param_block_t));
    return 0;
}
