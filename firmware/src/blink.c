// Toggles the Arduino Uno LED (D13 = PB5) every BLINK_HALF_PERIOD_MS (500 ms: 1 Hz) with busy-wait delays.
// The delay loops are cycle-counted by avr-libc: a 1 Hz blink validates the instruction timings of the emulator.
#include <avr/io.h>
#include <util/delay.h>

#ifndef BLINK_HALF_PERIOD_MS
#define BLINK_HALF_PERIOD_MS 500  // Compile-time constant: _delay_ms is cycle-counted by avr-libc
#endif

int main(void) {
    DDRB |= _BV(DDB5);
    for (;;) {
        PINB = _BV(PINB5);  // Writing a one to PINx toggles PORTx
        _delay_ms(BLINK_HALF_PERIOD_MS);
    }
}
