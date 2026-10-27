// Counts milliseconds with a Timer0 compare-match interrupt (CTC, clk/64, 250 ticks = 1 ms at 16 MHz), sleeps in idle
// mode between interrupts, and prints "t=<ms>" on USART0 every second.
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <stdlib.h>

#define BAUD 9600
#include <util/setbaud.h>

static volatile uint32_t milliseconds;

ISR(TIMER0_COMPA_vect) { ++milliseconds; }

static void uartPuts(const char* text) {
    while (*text) {
        loop_until_bit_is_set(UCSR0A, UDRE0);
        UDR0 = *text++;
    }
}

int main(void) {
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;
    UCSR0B = _BV(TXEN0);

    TCCR0A = _BV(WGM01);              // CTC
    OCR0A  = 249;                     // 250 timer ticks
    TCCR0B = _BV(CS01) | _BV(CS00);   // clk/64
    TIMSK0 = _BV(OCIE0A);

    set_sleep_mode(SLEEP_MODE_IDLE);
    sei();

    uint32_t next = 1000;
    for (;;) {
        sleep_mode();
        cli();
        const uint32_t now = milliseconds;
        sei();
        if (now >= next) {
            char number[11];
            uartPuts("t=");
            uartPuts(ultoa(now, number, 10));
            uartPuts("\r\n");
            next += 1000;
        }
    }
}
