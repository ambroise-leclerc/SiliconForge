// Toggles the Arduino Uno LED (D13 = PB5) every 500 ms with busy-wait delays.
// The delay loops are cycle-counted by avr-libc: a 1 Hz blink validates the instruction timings of the emulator.
#include <avr/io.h>
#include <util/delay.h>

int main(void) {
    DDRB |= _BV(DDB5);
    for (;;) {
        PINB = _BV(PINB5);  // Writing a one to PINx toggles PORTx
        _delay_ms(500);
    }
}
