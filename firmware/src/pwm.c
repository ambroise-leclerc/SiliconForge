// Timer0 Fast PWM at 62.5 kHz on both of its outputs: 25 % on D6 (OC0A, non-inverting) and 75 % on D5 (OC0B).
// This is how analogWrite() drives D5/D6 on an Arduino Uno, without the Arduino core.
#include <avr/io.h>

int main(void) {
    DDRD |= _BV(DDD6) | _BV(DDD5);
    OCR0A  = 63;   // (63 + 1) / 256 = 25 %
    OCR0B  = 191;  // (191 + 1) / 256 = 75 %
    TCCR0A = _BV(COM0A1) | _BV(COM0B1) | _BV(WGM01) | _BV(WGM00);  // Fast PWM, clear on compare match
    TCCR0B = _BV(CS00);                                            // clk/1
    for (;;) {
    }
}
