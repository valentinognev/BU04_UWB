# BU03/BU04 firmware build (GCC + OpenOCD)
# Target: STM32F103CB @ 128KB flash / 20KB RAM

SDK_ROOT := $(abspath .)
BUILD    := $(SDK_ROOT)/build
SPL      := $(SDK_ROOT)/ThirdParty/stm32f10x-stdperiph-lib-master/Libraries
CMSIS_CORE   := $(SDK_ROOT)/ThirdParty/STM32CubeF1/Drivers/CMSIS/Include
CMSIS_DEVICE := $(SPL)/CMSIS/CM3/DeviceSupport/ST/STM32F10x
STD_PERIPH   := $(SPL)/STM32F10x_StdPeriph_Driver
UWB_LIB      := $(SDK_ROOT)/Components/HAL/DW/twr_pdoa/lib
UWB_LIB_OBJ  := $(BUILD)/keil_objs
RTE          := $(SDK_ROOT)/Projects/USER/RTE/Device/STM32F103T8

TARGET := $(BUILD)/bu04_firmware
OPENOCD := openocd -s $(SDK_ROOT)
OPENOCD_CFG := $(SDK_ROOT)/openocd.cfg
OPENOCD_CFG_ANCHOR := $(SDK_ROOT)/openocd-anchor.cfg
OPENOCD_CFG_TAG := $(SDK_ROOT)/openocd-tag.cfg
# Anchor: ST-Link V2.1 serial 066CFF313050353043214925 → /dev/ttyUSB0
# Tag:    ST-Link V2.1 serial 0667FF313050353043212228 → /dev/ttyUSB4 (path 3-2.1.2)

CC  := arm-none-eabi-gcc
AS  := arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY := arm-none-eabi-objcopy
SIZE := arm-none-eabi-size

CPU  := -mcpu=cortex-m3 -mthumb
DEFS := -DSTM32F10X_MD -DUSE_STDPERIPH_DRIVER

INCLUDES := \
	-I$(SDK_ROOT)/Projects/USER \
	-I$(SDK_ROOT)/Projects/USER/AIT \
	-I$(SDK_ROOT)/Components/APP \
	-I$(SDK_ROOT)/Components/Main \
	-I$(SDK_ROOT)/Components/HAL/HAL \
	-I$(SDK_ROOT)/Components/HAL/OLED \
	-I$(SDK_ROOT)/Components/HAL/LIS2DH12 \
	-I$(SDK_ROOT)/Components/HAL/USB/src \
	-I$(SDK_ROOT)/Components/HAL/USB/lib \
	-I$(SDK_ROOT)/Components/HAL/DW/decadriver \
	-I$(SDK_ROOT)/Components/HAL/DW/twr_pdoa/inc \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_00a_reading_dev_id \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_01a_simple_tx \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_01d_tx_timed_sleep \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_01h_simple_tx_pdoa \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_02a_simple_rx \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_02h_simple_rx_pdoa \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_03a_tx_wait_resp \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_03b_rx_send_resp \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_03d_tx_wait_resp_interrupts \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_05c_ds_twr_init_sts_sdc \
	-I$(SDK_ROOT)/Components/Examples/examples/ex_05d_ds_twr_resp_sts_sdc \
	-I$(SDK_ROOT)/Components/Examples/examples/examples_info \
	-I$(CMSIS_CORE) \
	-I$(CMSIS_DEVICE) \
	-I$(STD_PERIPH)/inc \
	-I$(RTE)

CFLAGS  := $(CPU) $(DEFS) $(INCLUDES) -Wall -Wno-unused-parameter -O2 \
           -ffunction-sections -fdata-sections -std=gnu99 -fshort-wchar \
           -include $(SDK_ROOT)/Components/Main/gcc_port.h
ASFLAGS := $(CPU) $(DEFS) $(INCLUDES)
LDFLAGS := $(CPU) -T$(SDK_ROOT)/linker/STM32F103CB.ld \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/bu04_firmware.map \
           -Wl,--wrap=twr_rx_tag_timeout_cb -Wl,--wrap=twr_rx_error_cb \
           -Wl,--wrap=twr_tx_tag_cb -Wl,--wrap=twr_rx_tag_cb \
           -Wl,--wrap=twr_tx_node_cb -Wl,--wrap=twr_rx_node_cb \
           -Wl,--wrap=process_deca_irq \
           -Wl,--wrap=dwt_configure \
           -Wl,--wrap=port_set_dw_ic_spi_slowrate \
           -Wl,--wrap=port_set_dw_ic_spi_fastrate \
           -specs=nano.specs -specs=nosys.specs

APP_SRC := \
	Components/APP/cmd.c \
	Components/APP/cmd_fn.c \
	Components/APP/twr_diag.c \
	Components/APP/tag_bootstrap.c \
	Components/APP/uwb_status.c \
	Components/APP/uwb_pdoa.c \
	Components/APP/uwb_data.c \
	Components/APP/Generic.c \
	Components/APP/Generic_cmd.c \
	Components/APP/user_out.c

HAL_SRC := \
	Components/HAL/HAL/hal_drivers.c \
	Components/HAL/HAL/hal_flash.c \
	Components/HAL/HAL/hal_iic.c \
	Components/HAL/HAL/hal_led.c \
	Components/HAL/HAL/hal_spi.c \
	Components/HAL/HAL/hal_spi_wrapper.c \
	Components/HAL/HAL/hal_timer.c \
	Components/HAL/HAL/hal_usart.c \
	Components/HAL/HAL/hal_usb.c \
	Components/HAL/DW/decadriver/deca_device.c \
	Components/HAL/DW/decadriver/deca_mutex.c \
	Components/HAL/OLED/OLED_I2C.c \
	Components/HAL/LIS2DH12/lis2dh12.c

USB_SRC := \
	Components/HAL/USB/src/hw_config.c \
	Components/HAL/USB/src/Queue.c \
	Components/HAL/USB/src/usb_desc.c \
	Components/HAL/USB/src/usb_endp.c \
	Components/HAL/USB/src/usb_istr.c \
	Components/HAL/USB/src/usb_it.c \
	Components/HAL/USB/src/usb_prop.c \
	Components/HAL/USB/src/usb_pwr.c \
	Components/HAL/USB/lib/usb_core.c \
	Components/HAL/USB/lib/usb_init.c \
	Components/HAL/USB/lib/usb_int.c \
	Components/HAL/USB/lib/usb_mem.c \
	Components/HAL/USB/lib/usb_regs.c \
	Components/HAL/USB/lib/usb_sil.c

MAIN_SRC := \
	Components/Main/main.c \
	Components/Main/stm32f10x_it.c \
	Components/Main/keil_shims.c

EXAMPLE_SRC := \
	Components/Examples/examples/ex_00a_reading_dev_id/read_dev_id.c \
	Components/Examples/examples/ex_01a_simple_tx/simple_tx.c \
	Components/Examples/examples/ex_01d_tx_timed_sleep/tx_timed_sleep.c \
	Components/Examples/examples/ex_01h_simple_tx_pdoa/simple_tx_pdoa.c \
	Components/Examples/examples/ex_02a_simple_rx/simple_rx.c \
	Components/Examples/examples/ex_02h_simple_rx_pdoa/simple_rx_pdoa.c \
	Components/Examples/examples/ex_03a_tx_wait_resp/tx_wait_resp.c \
	Components/Examples/examples/ex_03b_rx_send_resp/rx_send_resp.c \
	Components/Examples/examples/ex_03d_tx_wait_resp_interrupts/tx_wait_resp_int.c \
	Components/Examples/examples/examples_info/example_info.c \
	Components/Examples/examples/ex_05c_ds_twr_init_sts_sdc/ds_twr_sts_sdc_init.c \
	Components/Examples/examples/ex_05d_ds_twr_resp_sts_sdc/ds_twr_sts_sdc_resp.c

SPL_REL := ThirdParty/stm32f10x-stdperiph-lib-master/Libraries/STM32F10x_StdPeriph_Driver/src

SPL_SRC := \
	$(SPL_REL)/misc.c \
	$(SPL_REL)/stm32f10x_adc.c \
	$(SPL_REL)/stm32f10x_bkp.c \
	$(SPL_REL)/stm32f10x_can.c \
	$(SPL_REL)/stm32f10x_cec.c \
	$(SPL_REL)/stm32f10x_crc.c \
	$(SPL_REL)/stm32f10x_dac.c \
	$(SPL_REL)/stm32f10x_dbgmcu.c \
	$(SPL_REL)/stm32f10x_dma.c \
	$(SPL_REL)/stm32f10x_exti.c \
	$(SPL_REL)/stm32f10x_flash.c \
	$(SPL_REL)/stm32f10x_fsmc.c \
	$(SPL_REL)/stm32f10x_gpio.c \
	$(SPL_REL)/stm32f10x_i2c.c \
	$(SPL_REL)/stm32f10x_iwdg.c \
	$(SPL_REL)/stm32f10x_pwr.c \
	$(SPL_REL)/stm32f10x_rcc.c \
	$(SPL_REL)/stm32f10x_rtc.c \
	$(SPL_REL)/stm32f10x_sdio.c \
	$(SPL_REL)/stm32f10x_spi.c \
	$(SPL_REL)/stm32f10x_tim.c \
	$(SPL_REL)/stm32f10x_usart.c \
	$(SPL_REL)/stm32f10x_wwdg.c \
	Projects/USER/RTE/Device/STM32F103T8/system_stm32f10x.c

ASM_SRC := \
	ThirdParty/stm32f10x-stdperiph-lib-master/Libraries/CMSIS/CM3/DeviceSupport/ST/STM32F10x/startup/gcc_ride7/startup_stm32f10x_md.s \
	Components/Main/keil_microlib_stubs.S

UWB_LIBS := \
	$(UWB_LIB)/aitcmd.lib \
	$(UWB_LIB)/os.lib \
	$(UWB_LIB)/pdoa.lib \
	$(UWB_LIB)/twr.lib

UWB_OBJS := $(shell find $(UWB_LIB_OBJ) -name '*.o' 2>/dev/null)

C_SRCS := $(APP_SRC) $(HAL_SRC) $(USB_SRC) $(MAIN_SRC) $(EXAMPLE_SRC) $(SPL_SRC)
C_OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst %.s,$(BUILD)/obj/%.o,$(ASM_SRC))

# VPATH for sources in different directories
vpath %.c $(sort $(dir $(C_SRCS)))
vpath %.s $(dir $(ASM_SRC))

.PHONY: all clean flash flash-anchor flash-tag flash-all reset-anchor reset-tag reset-all list-stlinks flash-vendor-refs-anchor flash-vendor-refs-tag flash-vendor-refs-all size erase-config deploy fetch-deps extract-keil-objs patch-uwb-spi patch-tag-poll-dest patch-tag-final-dest deps

all: extract-keil-objs patch-uwb-spi patch-tag-poll-dest patch-tag-final-dest $(TARGET).elf $(TARGET).hex $(TARGET).bin
	@$(SIZE) $(TARGET).elf

extract-keil-objs:
	@mkdir -p $(UWB_LIB_OBJ)
	@for lib in $(UWB_LIBS); do \
		name=$$(basename $$lib .lib); \
		mkdir -p $(UWB_LIB_OBJ)/$$name; \
		( cd $(UWB_LIB_OBJ)/$$name && arm-none-eabi-ar x $$lib ); \
		for obj in $(UWB_LIB_OBJ)/$$name/*.o; do \
			arm-none-eabi-objcopy --strip-unneeded $$obj $$obj; \
		done; \
	done

# Binary-patch twr/uwb.o:
# 1. port_set_dw_ic_spi_fastrate: set SPI prescaler /32 (2.25 MHz) instead of /2 (36 MHz).
#    DW3000 only allows ≤7 MHz during INIT_RC; at 36 MHz all reads return 0xFFFFFFFF.
# 2. Rename readfromspi_serial → readfromspi_serial_hw (and writetospi_serial_hw) so
#    our hal_spi_wrapper.c can intercept each transaction to set SSM=1 (software NSS).
#    Without SSM=1, SPI1 drops to slave mode when CS/PA4 goes LOW, killing the clock.
patch-uwb-spi:
	@arm-none-eabi-objcopy \
		--redefine-sym readfromspi_serial=readfromspi_serial_hw \
		--redefine-sym writetospi_serial=writetospi_serial_hw \
		--redefine-sym setup_DW1000RSTnIRQ=setup_DW1000RSTnIRQ_hw \
		--redefine-sym reset_DWIC=reset_DWIC_hw \
		$(UWB_LIB_OBJ)/twr/uwb.o \
		$(UWB_LIB_OBJ)/twr/uwb.o
	@echo "patch-uwb-spi: symbol rename complete"

# tag.o hardcodes poll dest 0xFFFF (broadcast). Anchor frame filter expects unicast.
# Load dest from tag+0x1ce, filled by tag_bootstrap from s_pdoa.addr (anc_id).
patch-tag-poll-dest:
	@python3 -c "\
import sys; \
f='$(UWB_LIB_OBJ)/twr/tag.o'; \
d=bytearray(open(f,'rb').read()); \
old=bytes.fromhex('4ff6ff728a80'); \
new=bytes.fromhex('b4f8ce218a80'); \
idx=d.find(old); \
sys.exit('patch-tag-poll-dest: pattern not found') if idx < 0 else None; \
d[idx:idx+4]=new[:4]; \
open(f,'wb').write(d); \
print('patch-tag-poll-dest: poll dest from tag+0x1ce applied'); \
"

# prepare_twr_final_msg also hardcodes dest 0xFFFF; load from tag+0x1ce (r5 base).
patch-tag-final-dest:
	@python3 -c "\
import sys; \
f='$(UWB_LIB_OBJ)/twr/tag.o'; \
d=bytearray(open(f,'rb').read()); \
old=bytes.fromhex('4ff6ff707880'); \
new=bytes.fromhex('b5f8ce017880'); \
idx=d.find(old); \
sys.exit('patch-tag-final-dest: pattern not found') if idx < 0 else None; \
d[idx:idx+4]=new[:4]; \
open(f,'wb').write(d); \
print('patch-tag-final-dest: final dest from tag+0x1ce applied'); \
"

deps:
	@test -f $(CMSIS_DEVICE)/stm32f10x.h || (echo "Run: make fetch-deps" && exit 1)

$(BUILD)/obj/%.o: %.c deps
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.s deps
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(C_OBJS) $(ASM_OBJS) extract-keil-objs
	$(CC) $(LDFLAGS) $(C_OBJS) $(ASM_OBJS) \
		-Wl,--start-group $(shell find $(UWB_LIB_OBJ) -name '*.o') -Wl,--end-group \
		-lm -lc -o $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

fetch-deps:
	@mkdir -p ThirdParty
	@if [ ! -d ThirdParty/stm32f10x-stdperiph-lib-master ]; then \
		curl -fsSL -o /tmp/spl.zip \
		  "https://codeload.github.com/wajatimur/stm32f10x-stdperiph-lib/zip/refs/heads/master"; \
		unzip -q -o /tmp/spl.zip -d ThirdParty/; \
		rm -rf ThirdParty/stm32f10x-stdperiph-lib-master/Project \
		       ThirdParty/stm32f10x-stdperiph-lib-master/Utilities \
		       ThirdParty/stm32f10x-stdperiph-lib-master/_htmresc; \
		rm -f ThirdParty/stm32f10x-stdperiph-lib-master/*.chm \
		      ThirdParty/stm32f10x-stdperiph-lib-master/Release_Notes.html; \
	fi
	@test -f ThirdParty/STM32CubeF1/Drivers/CMSIS/Include/core_cm3.h || \
		(echo "Missing CMSIS headers — restore ThirdParty/STM32CubeF1 from repo." && exit 1)

# OpenOCD flash helpers (use "halt", never "reset init" on a running BU04).
# Split erase + write avoids "timeout waiting for algorithm" from write_image erase.
# Use "reset halt" not bare "halt" — running UWB firmware often ignores debug halt.
OPENOCD_FLASH_SLOW = adapter speed 100;
OPENOCD_FLASH_BODY = init; reset halt; flash erase_address 0x08000000 0x20000; halt; flash write_image $(TARGET).bin 0x08000000; verify_image $(TARGET).bin 0x08000000; reset run; shutdown
# Tag ST-Link V2: if firmware is in HardFault (pc 0xfffffffe), "reset halt" times out.
# Halt the faulted core directly, then erase/write without an extra reset.
# SRST "reset run" often leaves PC in SRAM; jump to the image reset vector explicitly.
VECTOR_SP = $(shell python3 -c "import struct;print('0x%08x'%struct.unpack('<I',open('$(TARGET).bin','rb').read(4))[0])" 2>/dev/null)
VECTOR_PC = $(shell python3 -c "import struct;print('0x%08x'%struct.unpack('<I',open('$(TARGET).bin','rb').read()[4:8])[0])" 2>/dev/null)
OPENOCD_TAG_BOOT = reg sp $(VECTOR_SP); reg pc $(VECTOR_PC); resume
OPENOCD_FLASH_BODY_TAG = init; halt; flash erase_address 0x08000000 0x20000; flash write_image $(TARGET).bin 0x08000000; verify_image $(TARGET).bin 0x08000000; $(OPENOCD_TAG_BOOT); shutdown
OPENOCD_ERASE_BODY = init; reset halt; flash erase_sector 0 127 127; reset run; shutdown
OPENOCD_ERASE_BODY_TAG = init; halt; flash erase_sector 0 127 127; $(OPENOCD_TAG_BOOT); shutdown
OPENOCD_RESET_BODY = init; reset run; shutdown
OPENOCD_RESET_BODY_TAG = init; halt; $(OPENOCD_TAG_BOOT); shutdown

define run_openocd
	$(OPENOCD) -f $(1) -c "$(2)" || \
		($(OPENOCD) -f $(1) -c "$(OPENOCD_FLASH_SLOW) $(2)")
endef

list-stlinks:
	@echo "Expected mapping (verify after USB replug):"
	@echo "  anchor / ttyUSB0: ST-Link V2.1  serial=066CFF313050353043214925  path=3-3"
	@echo "  tag    / ttyUSB4: ST-Link V2.1  serial=0667FF313050353043212228  path=3-2.1.2"
	@echo ""
	@lsusb | grep -i 'ST-LINK\|STMicroelectronics' || true
	@echo ""
	@for d in /sys/bus/usb/devices/*; do \
		if [ -f "$$d/idVendor" ] && [ "$$(cat $$d/idVendor 2>/dev/null)" = "0483" ]; then \
		 echo "  $$(basename $$d)  pid=$$(cat $$d/idProduct 2>/dev/null)  serial=$$(cat $$d/serial 2>/dev/null)"; \
		fi; \
	done 2>/dev/null || true

flash: flash-anchor

flash-anchor:
	@test -f $(TARGET).bin || (echo "No $(TARGET).bin — run 'make' to build first." && exit 1)
	@echo "Flashing anchor (ST-Link V2.1 066CFF...4925 → ttyUSB0)..."
	$(call run_openocd,$(OPENOCD_CFG_ANCHOR),$(OPENOCD_FLASH_BODY))

flash-tag:
	@test -f $(TARGET).bin || (echo "No $(TARGET).bin — run 'make' to build first." && exit 1)
	@echo "Flashing tag (ST-Link V2 path 3-2.1.3 → ttyUSB4)..."
	$(call run_openocd,$(OPENOCD_CFG_TAG),$(OPENOCD_FLASH_BODY_TAG))

flash-all: flash-anchor flash-tag
	@echo "Flashed both modules."

reset-anchor:
	@echo "Reset anchor MCU (ST-Link V2.1 → ttyUSB0)..."
	$(OPENOCD) -f $(OPENOCD_CFG_ANCHOR) -c "$(OPENOCD_RESET_BODY)"

reset-tag:
	@echo "Reset tag MCU (ST-Link V2 → ttyUSB4)..."
	$(OPENOCD) -f $(OPENOCD_CFG_TAG) -c "$(OPENOCD_RESET_BODY_TAG)"

reset-all: reset-anchor reset-tag

VENDOR_HEX := $(abspath $(SDK_ROOT)/../Refs/（2717）BU03_BU04-AT-通用-常规固件V1.0.0_T2024-07-26.hex)

flash-vendor-refs-anchor:
	@test -f "$(VENDOR_HEX)" || (echo "Missing vendor hex: $(VENDOR_HEX)" && exit 1)
	@echo "Flashing vendor hex to anchor..."
	$(call run_openocd,$(OPENOCD_CFG_ANCHOR),init\; halt\; flash erase_address 0x08000000 0x20000\; program $(VENDOR_HEX) verify\; reset run\; shutdown)

flash-vendor-refs-tag:
	@test -f "$(VENDOR_HEX)" || (echo "Missing vendor hex: $(VENDOR_HEX)" && exit 1)
	@echo "Flashing vendor hex to tag..."
	$(call run_openocd,$(OPENOCD_CFG_TAG),init\; halt\; flash erase_address 0x08000000 0x20000\; program $(VENDOR_HEX) verify\; reset run\; shutdown)

flash-vendor-refs-all: flash-vendor-refs-anchor flash-vendor-refs-tag
	@echo "Flashed vendor hex on both modules."

size: $(TARGET).elf
	$(SIZE) $(TARGET).elf

clean:
	rm -rf $(BUILD)

erase-config:
	$(call run_openocd,$(OPENOCD_CFG_ANCHOR),$(OPENOCD_ERASE_BODY))

erase-config-tag:
	$(call run_openocd,$(OPENOCD_CFG_TAG),$(OPENOCD_ERASE_BODY_TAG))

# Single OpenOCD session: do not reset-run between NVM erase and firmware program.
deploy: erase-config flash-anchor
	@echo "Deployed anchor: NVM erased (page 127 @ 0x0801FC00) and firmware flashed."

deploy-all: erase-config erase-config-tag flash-all
	@echo "Deployed both modules: NVM erased and firmware flashed."
