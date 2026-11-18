// Duty-cycle setpoint on the demonstration circuit (circuits/arduino-demo.json), the subject of the bench scenario
// scenarios/pwm-setpoint.json: Timer0 Fast PWM at 62.5 kHz on D6 (OC0A) follows a setpoint chosen by the button on
// D2, and the LED on D13 acknowledges the high setpoint once it is applied.
#include <avr/io.h>
#include <stdint.h>

#ifndef SETPOINT_RELEASED_PERCENT
#define SETPOINT_RELEASED_PERCENT 25
#endif
#ifndef SETPOINT_PRESSED_PERCENT
#define SETPOINT_PRESSED_PERCENT 75
#endif

// Fast PWM, non-inverting: the output is high for OCR0A + 1 counts out of 256.
static uint8_t compareFor(uint8_t percent) {
    const uint16_t counts = (uint16_t)percent * 256u / 100u;
    return counts == 0 ? 0 : (uint8_t)(counts - 1u);
}

int main(void) {
    DDRD |= _BV(DDD6);
    DDRB |= _BV(DDB5);    // D13
    PORTD |= _BV(PORTD2); // The button connects D2 to ground: pull-up
    OCR0A  = compareFor(SETPOINT_RELEASED_PERCENT);
    TCCR0A = _BV(COM0A1) | _BV(WGM01) | _BV(WGM00);
    TCCR0B = _BV(CS00);
    for (;;) {
        const uint8_t pressed = !(PIND & _BV(PIND2));
        OCR0A                 = compareFor(pressed ? SETPOINT_PRESSED_PERCENT : SETPOINT_RELEASED_PERCENT);
        if (pressed)
            PORTB |= _BV(PORTB5);
        else
            PORTB &= ~_BV(PORTB5);
    }
}
