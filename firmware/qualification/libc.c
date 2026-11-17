// A selection of avr-libc: formatted output and input, sorting, string and memory functions, program memory accesses.
// Tests of avr-libc's own suite can be added here in the same way (docs/QUALIFICATION.md).
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qualification.h"

static int compare(const void* a, const void* b) { return *(const int16_t*)a - *(const int16_t*)b; }

static const char banner[] PROGMEM = "SiliconForge";

int main(void) {
    char buffer[48];
    snprintf(buffer, sizeof buffer, "%d|%5u|%-4x|%08lX|%s|%c", -42, 7u, 0xAB, 0xBEEFUL, "ok", 'z');
    check(strcmp(buffer, "-42|    7|ab  |0000BEEF|ok|z") == 0);
    check(snprintf(buffer, OPAQUE(8), "%s", "truncated output") == 16 && strlen(buffer) == 7);

    int      i;
    unsigned u;
    char     word[8];
    check(sscanf("  123 0x1f hello", "%d %x %7s", &i, &u, word) == 3 && i == 123 && u == 31 && strcmp(word, "hello") == 0);

    check(strtol("-0x7fffffff", 0, 16) == -0x7fffffffL);
    check(strtoul("4294967295", 0, 10) == 4294967295UL);
    char* end;
    check(strtol("  77z", &end, 8) == 63 && *end == 'z');
    check(atoi("-32768") == -32768);

    int16_t values[] = {5, -3, 1000, 0, -32000, 42, 7, 7, -1};
    qsort(values, sizeof values / sizeof values[0], sizeof values[0], compare);
    uint8_t sorted = 1;
    for (uint8_t k = 1; k < sizeof values / sizeof values[0]; ++k)
        sorted &= values[k - 1] <= values[k];
    check(sorted && values[0] == -32000 && values[8] == 1000);

    char move[] = "abcdefgh";
    memmove(move + 2, move, 5);
    check(memcmp(move, "ababcdeh", 8) == 0);
    check(strstr("emulated cycles", "cycles") != 0);
    check(strncmp("ATmega328P", "ATmega168", 6) == 0);
    check(strlen_P(banner) == 12 && strcmp_P("SiliconForge", banner) == 0);
    check(pgm_read_byte(&banner[7]) == 'F');
    ltoa(-1234567L, buffer, 10);
    check(strcmp(buffer, "-1234567") == 0);
    utoa(0xC0DE, buffer, 16);
    check(strcmp(buffer, "c0de") == 0);
    finish();
}
