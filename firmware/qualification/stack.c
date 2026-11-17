// A deep stack: recursion with frames on the stack (Ackermann, mutual recursion), calls through function pointers,
// large locals and variadic arguments. The stack pointer must come back to its value, and nothing above it must move.
#include <avr/io.h>
#include <stdarg.h>
#include <string.h>

#include "qualification.h"

static uint16_t lowest = 0xFFFF;
static uint8_t  calls, deepest;

static void watch(void) {
    if (SP < lowest)
        lowest = SP;
}

static uint16_t __attribute__((noinline)) ackermann(uint16_t m, uint16_t n) {
    watch();
    if (++calls > deepest)
        deepest = calls;
    uint16_t result;
    if (m == 0)
        result = n + 1;
    else if (n == 0)
        result = ackermann(m - 1, 1);
    else
        result = ackermann(m - 1, ackermann(m, n - 1));
    --calls;  // After the calls: no tail call, every level keeps its frame
    return result;
}

static uint8_t isOdd(uint8_t n);
static uint8_t __attribute__((noinline)) isEven(uint8_t n) { watch(); return n == 0 ? 1 : isOdd(n - 1); }
static uint8_t __attribute__((noinline)) isOdd(uint8_t n) { watch(); return n == 0 ? 0 : isEven(n - 1); }

static uint32_t __attribute__((noinline)) sum(uint8_t count, ...) {
    va_list arguments;
    va_start(arguments, count);
    uint32_t total = 0;
    while (count--)
        total += va_arg(arguments, uint32_t);
    va_end(arguments);
    return total;
}

static uint16_t __attribute__((noinline)) checksum(uint8_t seed) {
    uint8_t block[200];  // A large frame
    for (uint8_t i = 0; i < sizeof block; ++i)
        block[i] = (uint8_t)(seed + i * 7);
    uint16_t total = 0;
    for (uint8_t i = 0; i < sizeof block; ++i)
        total = (uint16_t)(total * 31 + block[i]);
    watch();
    return total;
}

static uint16_t twice(uint16_t x) { return x * 2; }
static uint16_t square(uint16_t x) { return x * x; }

int main(void) {
    static const char guard[] = "above the stack";
    char              copy[sizeof guard];
    memcpy(copy, guard, sizeof guard);
    const uint16_t sp = SP;

    check(ackermann(OPAQUE(2), OPAQUE(3)) == 9);
    check(ackermann(OPAQUE(3), OPAQUE(3)) == 61);
    check(isEven(OPAQUE(150)));
    check(!isEven(OPAQUE(151)));
    check(sum(OPAQUE(4), 1UL, 2UL, 3UL, 4000000000UL) == 4000000006UL);
    check(checksum(OPAQUE(3)) == checksum(OPAQUE(3)));
    uint16_t (*volatile operations[])(uint16_t) = {twice, square};
    check(operations[0](OPAQUE(21)) == 42 && operations[1](OPAQUE(12)) == 144);

    check(SP == sp);
    check(deepest == 63);                          // A(3,3) nests 63 calls...
    check(sp - lowest > 2 * 63);                   // ...each with at least its return address on the stack
    check(memcmp(copy, guard, sizeof guard) == 0);
    finish();
}
