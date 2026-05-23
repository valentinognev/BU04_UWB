#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define __packed __attribute__((packed))

#include "/home/valentin/RL/UWB/Refs/STM32F103-BU0x_SDK/Components/HAL/DW/decadriver/deca_device_api.h"
#include "/home/valentin/RL/UWB/Refs/STM32F103-BU0x_SDK/Components/HAL/DW/twr_pdoa/inc/default_config.h"

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
