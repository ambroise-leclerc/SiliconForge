# Test firmware for the emulated ATmega328P (Arduino Uno: 16 MHz).
#   make            builds every firmware into build/
#   make prebuilt   also refreshes the binaries committed in prebuilt/, used by the integration tests
MCU    ?= atmega328p
F_CPU  ?= 16000000UL
CC     := avr-gcc
OBJCOPY:= avr-objcopy
CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -g -std=gnu11 -Wall -Wextra -Werror

FIRMWARE := blink uart_echo timer0_isr pwm
ELF := $(FIRMWARE:%=build/%.elf)
HEX := $(FIRMWARE:%=build/%.hex)

all: $(ELF) $(HEX)

build/%.elf: src/%.c | build
	$(CC) $(CFLAGS) -o $@ $<

build/%.hex: build/%.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

build:
	mkdir -p $@

prebuilt: all
	cp $(HEX) prebuilt/
	cp build/blink.elf prebuilt/

clean:
	rm -rf build

.PHONY: all prebuilt clean
