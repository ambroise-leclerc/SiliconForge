// Nested interrupts: the compare match A of Timer0 masks itself, re-enables the interrupts and waits inside its handler
// for the overflow interrupt, which must preempt it. Checks the order, the nesting depth, the registers saved by the
// handlers and that the main program resumes untouched.
#include <avr/interrupt.h>
#include <avr/io.h>

#include "qualification.h"

static volatile uint8_t depth, deepest, overflows, compares, order[8], events;

static void note(uint8_t what) {
    if (events < sizeof order)
        order[events] = what;
    ++events;
}

ISR(TIMER0_COMPA_vect) {
    TIMSK0 &= (uint8_t)~_BV(OCIE0A);  // Not re-entered by its own next match, however slow the handler is
    sei();
    ++depth;
    if (depth > deepest)
        deepest = depth;
    note('A');
    const uint8_t before = overflows;
    while (overflows == before) {  // Interrupts are enabled: the overflow preempts this handler.
    }
    note('a');
    ++compares;
    --depth;
    cli();
    TIMSK0 |= _BV(OCIE0A);
}

ISR(TIMER0_OVF_vect) {
    ++depth;
    if (depth > deepest)
        deepest = depth;
    note('O');
    ++overflows;
    --depth;
}

int main(void) {
    // Registers the compiler keeps across the waits: any corruption by the handlers shows here.
    volatile uint32_t sentinel = 0xDEADBEEFUL;
    OCR0A  = 100;
    TCCR0A = 0;               // Normal mode: overflow at 256, compare match at 100
    TIMSK0 = _BV(OCIE0A) | _BV(TOIE0);
    TCCR0B = _BV(CS01);       // clk/8: a period of 2048 cycles
    sei();
    while (compares < 3) {
    }
    cli();
    check(deepest == 2);
    check(compares == 3);
    check(overflows >= 3);
    check(order[0] == 'A' && order[1] == 'O' && order[2] == 'a');
    check(order[3] == 'A' && order[4] == 'O' && order[5] == 'a');
    check(sentinel == 0xDEADBEEFUL);
    check(!(SREG & _BV(SREG_I)));
    finish();
}
