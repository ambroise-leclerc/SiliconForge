# Test firmware for the emulated ATmega328P (Arduino Uno: 16 MHz).
#   make            builds every firmware and the test variants into $(BUILD) (default build/; CMake uses its own)
#   make prebuilt   also refreshes the binaries committed in prebuilt/, used by the integration tests
BUILD  ?= build
MCU    ?= atmega328p
F_CPU  ?= 16000000UL
CC     := avr-gcc
OBJCOPY:= avr-objcopy
CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -g -std=gnu11 -Wall -Wextra -Werror

FIRMWARE := blink uart_echo timer0_isr pwm demo
# Variants are built for the tests only and are not committed: blink with a 250 ms half-period (2 Hz).
VARIANTS := blink_250ms
ELF := $(FIRMWARE:%=$(BUILD)/%.elf)
HEX := $(FIRMWARE:%=$(BUILD)/%.hex)

all: $(ELF) $(HEX) $(VARIANTS:%=$(BUILD)/%.hex)

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

.PHONY: all prebuilt clean
