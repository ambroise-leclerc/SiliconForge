// The 7-segment circuit (circuits/seven-segment.json): counts from 0 to 9 on a common-cathode display, one digit every
// 500 ms. Segments a to f are on D2 to D7 (PD2..PD7), g on D8 (PB0) and the decimal point on D9 (PB1), which lights on
// odd digits. Each digit is also sent on the serial port at 9600 baud, for the serial monitor and the logic analyser.
#include <avr/io.h>
#include <util/delay.h>

// Bit 0 is segment a, bit 6 is segment g.
static const uint8_t digits[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

static void send(char c) {
    while (!(UCSR0A & _BV(UDRE0))) {}
    UDR0 = c;
}

int main(void) {
    DDRD |= 0xFC;
    DDRB |= _BV(DDB0) | _BV(DDB1);
    UBRR0 = F_CPU / 16 / 9600 - 1;
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    for (;;) {
        for (uint8_t digit = 0; digit < 10; ++digit) {
            const uint8_t segments = digits[digit];
            PORTD = (PORTD & 0x03) | (uint8_t)(segments << 2);
            PORTB = (PORTB & ~(_BV(PORTB0) | _BV(PORTB1))) | (segments >> 6) | (digit & 1 ? _BV(PORTB1) : 0);
            send('0' + digit);
            send('\r');
            send('\n');
            _delay_ms(500);
        }
    }
}
