// setjmp/longjmp of avr-libc: the saved registers, stack pointer and status register are restored from deep calls.
#include <avr/io.h>
#include <setjmp.h>

#include "qualification.h"

static jmp_buf  context;
static volatile uint8_t depthReached;

// Each path either recurses or leaves through longjmp: that is the point, not an infinite recursion.
#pragma GCC diagnostic ignored "-Winfinite-recursion"
static void __attribute__((noinline)) descend(uint8_t depth) {
    volatile uint8_t padding[16];  // Real frames, so that SP moves.
    padding[0] = depth;
    depthReached = padding[0];
    if (depth == 20)
        longjmp(context, depth + 1);
    descend(depth + 1);
}

int main(void) {
    volatile uint16_t kept = 0x1234;
    const uint16_t    sp   = SP;
    const int         back = setjmp(context);
    if (back == 0) {
        kept = 0x5678;
        descend(1);
        check(0);  // Not reached
    }
    check(back == 21);
    check(depthReached == 20);
    check(kept == 0x5678);  // A volatile local keeps its last value
    check(SP == sp);        // The stack is back where setjmp left it

    // longjmp(…, 0) returns 1, as the standard requires.
    if (setjmp(context) == 0)
        longjmp(context, 0);
    else
        check(1);

    // Interrupts disabled by setjmp's caller stay as they were saved.
    SREG |= _BV(SREG_T);
    if (setjmp(context) == 0) {
        SREG &= (uint8_t)~_BV(SREG_T);
        longjmp(context, 1);
    }
    check(SREG & _BV(SREG_T));
    finish();
}
