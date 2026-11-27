// AVR-LibC startup must copy the initial value from Flash into SRAM before main.
#include <stdint.h>

volatile uint8_t initialized = 42;

static uint8_t read_initialized(void) {
    return initialized;
}

uint8_t (*volatile read_value)(void) = read_initialized;

int main(void) {
    if (read_value() == 42)
        __asm__ volatile("break");
    for (;;) {}
}
