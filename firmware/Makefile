# Test firmware for the emulated AVR targets.
#   make            builds every firmware and the test variants into $(BUILD) (default build/; CMake uses its own)
#   make prebuilt   also refreshes the binaries committed in prebuilt/, used by the integration tests
BUILD  ?= build
MCU    ?= atmega328p
F_CPU  ?= 16000000UL
CC     := avr-gcc
OBJCOPY:= avr-objcopy
CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -g -std=gnu11 -Wall -Wextra -Werror

FIRMWARE := blink uart_echo timer0_isr pwm demo counter buttons pwm_setpoint tft_spi tft_parallel lcd1602 lcd1602_i2c
# Variants are built for the tests only and are not committed: blink with a 250 ms half-period (2 Hz).
VARIANTS := blink_250ms
ELF := $(FIRMWARE:%=$(BUILD)/%.elf)
HEX := $(FIRMWARE:%=$(BUILD)/%.hex)

all: $(ELF) $(HEX) $(VARIANTS:%=$(BUILD)/%.hex) $(BUILD)/atmega2560_startup.elf $(BUILD)/attiny424_rodata.elf

$(BUILD)/atmega2560_startup.elf: src/atmega2560_startup.c | $(BUILD)
	$(CC) $(filter-out -mmcu=%,$(CFLAGS)) -mmcu=atmega2560 -o $@ $<

# Uses only common AVR instructions. The test linker script supplies the ATtiny424 memory layout.
$(BUILD)/attiny424_rodata.elf: src/attiny424_rodata.S src/attiny424_rodata.ld | $(BUILD)
	$(CC) -mmcu=avr5 -nostdlib -Wl,-T,src/attiny424_rodata.ld -o $@ $<

$(BUILD)/%.elf: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/blink_250ms.elf: src/blink.c | $(BUILD)
	$(CC) $(CFLAGS) -DBLINK_HALF_PERIOD_MS=250 -o $@ $<

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

$(BUILD):
	mkdir -p $@

prebuilt: all
	cp $(HEX) prebuilt/
	cp $(BUILD)/blink.elf prebuilt/

clean:
	rm -rf $(BUILD)

# The firmware whose binaries are committed in prebuilt/ (not the test variants), one per line, for the CI.
list:
	@printf '%s\n' $(FIRMWARE)

.PHONY: all prebuilt clean list
