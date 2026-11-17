# Qualification programs (ADR 0006): every program at three optimisation levels, each reporting its verdict on the
# reserved I/O bytes of qualification.h. Built by CMake into its binary directory; nothing is committed.
#   make BUILD=<directory>
BUILD  ?= build
MCU    ?= atmega328p
F_CPU  ?= 16000000UL
CC     := avr-gcc
OBJCOPY:= avr-objcopy
LEVELS := O0 O2 Os
CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function

PROGRAMS := $(basename $(wildcard *.c))
HEX := $(foreach program,$(PROGRAMS),$(foreach level,$(LEVELS),$(BUILD)/$(program)-$(level).hex))

all: $(HEX)

define level_rule
$(BUILD)/%-$(1).elf: %.c qualification.h | $(BUILD)
	$(CC) $(CFLAGS) -$(1) -o $$@ $$< -lm
endef
$(foreach level,$(LEVELS),$(eval $(call level_rule,$(level))))

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.SECONDARY:
.PHONY: all clean
