// The demonstration circuit in one program: the red LED on D13 blinks at 1 Hz and the green LED on D6 "breathes" with
// Timer0 Fast PWM (OC0A), its duty cycle rising and falling linearly with a 2 s period.
#include <avr/io.h>
#include <util/delay.h>

int main(void) {
    DDRB |= _BV(DDB5);
    DDRD |= _BV(DDD6);
    TCCR0A = _BV(COM0A1) | _BV(WGM01) | _BV(WGM00);  // Fast PWM, non-inverting on OC0A (D6)
    TCCR0B = _BV(CS01);                              // clk/8: 7.8 kHz, far above what the eye resolves
    for (;;) {
        for (uint8_t step = 0; step < 200; ++step) {  // 200 steps of 10 ms: one 2 s breathing cycle
            if (step % 50 == 0)
                PINB = _BV(PINB5);  // Writing a one to PINx toggles PORTx: every 0.5 s
            OCR0A = (step < 100 ? step : 199 - step) * 5 / 2;  // 0..247..0, peak after 1 s
            _delay_ms(10);
        }
    }
}
