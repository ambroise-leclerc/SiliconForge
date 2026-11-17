// Verdict of a qualification program (ADR 0006, docs/QUALIFICATION.md).
//
// A program counts its checks and reports through four bytes of the extended I/O space that the ATmega328P leaves
// reserved, then executes BREAK:
//   0xFF  verdict: 0x01 passed, 0x02 failed (0x00 while running)
//   0xFE  number of the first failed check (1-based), 0 when none failed
//   0xFC  number of checks, low byte; 0xFD high byte
// siliconforge_qualification reads them when the emulation stops on BREAK.
#ifndef SILICONFORGE_QUALIFICATION_H
#define SILICONFORGE_QUALIFICATION_H

#include <stdint.h>

#define SF_VERDICT     (*(volatile uint8_t*)0xFF)
#define SF_FIRST_FAIL  (*(volatile uint8_t*)0xFE)
#define SF_CHECKS_HIGH (*(volatile uint8_t*)0xFD)
#define SF_CHECKS_LOW  (*(volatile uint8_t*)0xFC)

static uint16_t sfChecks;
static uint8_t  sfFirstFail;

/// Records one check. Not inlined, so that the check itself is not optimised differently from one level to another.
static void __attribute__((noinline)) check(uint8_t passed) {
    ++sfChecks;
    if (!passed && sfFirstFail == 0)
        sfFirstFail = sfChecks > 255 ? 255 : (uint8_t)sfChecks;
}

static void __attribute__((noreturn)) finish(void) {
    SF_CHECKS_LOW  = (uint8_t)sfChecks;
    SF_CHECKS_HIGH = (uint8_t)(sfChecks >> 8);
    SF_FIRST_FAIL  = sfFirstFail;
    SF_VERDICT     = sfFirstFail == 0 && sfChecks > 0 ? 0x01 : 0x02;
    for (;;)
        __asm__ volatile("break");
}

/// Hides a value from the optimiser: the computation happens at run time, on the emulated core.
#define OPAQUE(x) ({ __typeof__(x) opaque_ = (x); __asm__ volatile("" : "+r"(opaque_)); opaque_; })

#endif
