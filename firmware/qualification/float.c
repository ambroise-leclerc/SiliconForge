// Software floating point of libgcc and avr-libc (single precision: avr-gcc's float and double). The products, sums
// and quotients are exactly rounded: their expected bit patterns were computed on the host (Python, IEEE 754 binary32
// rounding). The functions of libm are checked within a relative tolerance.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qualification.h"

static uint32_t bits(float value) {
    uint32_t result;
    memcpy(&result, &value, sizeof result);
    return result;
}

static uint8_t near(float value, float expected, float tolerance) {
    return fabsf(value - expected) <= tolerance * fabsf(expected);
}

int main(void) {
    const float a = OPAQUE(1.1f), b = OPAQUE(3.3f), c = OPAQUE(-0.0625f), big = OPAQUE(3.0e38f), tiny = OPAQUE(1.0e-38f);
    check(bits(a + b) == 0x408ccccdUL);  // 4.4
    check(bits(a * b) == 0x406851ecUL);  // 3.63
    check(bits(b / a) == 0x40400000UL);  // 3
    check(bits(a - b) == 0xc00cccccUL);  // -2.2
    check(bits(c * c) == 0x3b800000UL);  // exact
    check(bits(big * OPAQUE(10.0f)) == 0x7f800000UL);  // overflow to +infinity
    check(isinf(big * OPAQUE(-10.0f)) && big * OPAQUE(-10.0f) < 0);
    check(tiny * OPAQUE(1.0e-10f) == 0.0f);  // avr-libc flushes the subnormals to zero
    check(isnan(OPAQUE(0.0f) / OPAQUE(0.0f)));

    // Conversions
    check((long)OPAQUE(-123456.75f) == -123456L);
    check((unsigned long)OPAQUE(4000000000.0f) == 4000000000UL);
    check(bits((float)OPAQUE(16777217L)) == 0x4b800000UL);  // 2^24 + 1 rounds to 2^24
    check(bits((float)OPAQUE(-1L)) == 0xbf800000UL);

    // libm
    check(near(sqrtf(OPAQUE(2.0f)), 1.41421356f, 1e-7f));
    check(near(sinf(OPAQUE(0.5f)), 0.479425539f, 1e-6f));
    check(near(cosf(OPAQUE(1.0f)), 0.540302306f, 1e-6f));
    check(near(expf(OPAQUE(1.0f)), 2.718281828f, 1e-6f));
    check(near(logf(OPAQUE(10.0f)), 2.302585093f, 1e-6f));
    check(near(atan2f(OPAQUE(1.0f), OPAQUE(-1.0f)), 2.35619449f, 1e-6f));
    check(near(powf(OPAQUE(2.0f), OPAQUE(10.5f)), 1448.15468f, 1e-6f));

    // Text conversions
    check(near(strtod("-6.0221e23", 0), -6.0221e23f, 1e-6f));
    check(near(atof("0.000125"), 0.000125f, 1e-6f));
    finish();
}
