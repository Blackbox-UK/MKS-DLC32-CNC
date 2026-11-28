# TFT43 Grbl firmware for the BigTreeTech TFT43 V3.0 (STM32F207VCT6)
# Needs arm-none-eabi-gcc (Ubuntu: apt install gcc-arm-none-eabi libnewlib-arm-none-eabi)

FW_VERSION ?= 0.1.0
# The BTT bootloader flashes any file named BIGTREE_TFT43_V3.0*.bin from the SD card root
BIN_NAME   = BIGTREE_TFT43_V3.0.grbl-$(FW_VERSION)
BUILD      = build

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

SRCS = src/main.c src/hw.c src/lcd_stm32.c src/gfx.c src/fonts.c src/touch_stm32.c \
       src/uart_stm32.c src/grbl.c src/ui.c \
       vendor/cmsis/system_stm32f2xx.c \
       vendor/fwlib/src/misc.c vendor/fwlib/src/stm32f2xx_rcc.c vendor/fwlib/src/stm32f2xx_gpio.c \
       vendor/fwlib/src/stm32f2xx_fsmc.c vendor/fwlib/src/stm32f2xx_usart.c \
       vendor/fwlib/src/stm32f2xx_flash.c
ASRC = vendor/cmsis/startup_stm32f2xx.S

CPU    = -mcpu=cortex-m3 -mthumb
DEFS   = -DSTM32F2XX -DUSE_STDPERIPH_DRIVER -DHSE_VALUE=8000000ul -DFW_VERSION=\"$(FW_VERSION)\"
INCS   = -Isrc -Ivendor/cmsis -Ivendor/fwlib/inc
CFLAGS = $(CPU) $(DEFS) $(INCS) -Os -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
         -ffunction-sections -fdata-sections -fno-common
VFLAGS = -w   # vendor code: ST/BTT sources, not ours to fix
LDFLAGS = $(CPU) -T ld/app_0x8008000.ld --specs=nano.specs --specs=nosys.specs \
          -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(BIN_NAME).map

OBJS = $(SRCS:%.c=$(BUILD)/%.o) $(ASRC:%.S=$(BUILD)/%.o)

all: $(BUILD)/$(BIN_NAME).bin

$(BUILD)/src/%.o: src/%.c src/*.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vendor/%.o: vendor/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(VFLAGS) -c $< -o $@

$(BUILD)/vendor/%.o: vendor/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -c $< -o $@

$(BUILD)/$(BIN_NAME).elf: $(OBJS) ld/app_0x8008000.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD)/$(BIN_NAME).bin: $(BUILD)/$(BIN_NAME).elf
	$(OBJCOPY) -O binary $< $@
	@ls -l $@

# PC simulator: renders each screen to PNG (needs gcc and python3 with Pillow)
sim:
	$(MAKE) -C sim

clean:
	rm -rf $(BUILD)
	$(MAKE) -C sim clean 2>/dev/null || true

.PHONY: all sim clean
