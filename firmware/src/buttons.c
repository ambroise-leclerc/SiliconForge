// The "LED and two buttons" circuit (circuits/led-buttons.json): the LED on D9 is lit while the button on D2 is held,
// and each press of the button on D3 toggles it for good. Both buttons connect their pin to ground; the firmware enables
// the internal pull-ups.
#include <avr/io.h>
#include <util/delay.h>

int main(void) {
    DDRB |= _BV(DDB1);
    PORTD |= _BV(PORTD2) | _BV(PORTD3);  // Pull-ups
    uint8_t latched = 0;
    uint8_t wasPressed = 0;
    for (;;) {
        const uint8_t held = !(PIND & _BV(PIND2));
        const uint8_t pressed = !(PIND & _BV(PIND3));
        if (pressed && !wasPressed)
            latched = !latched;
        wasPressed = pressed;
        if (held || latched)
            PORTB |= _BV(PORTB1);
        else
            PORTB &= ~_BV(PORTB1);
        _delay_ms(5);  // Also debounces a real button
    }
}
