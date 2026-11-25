// An LCD1602 (HD44780) on a 4-bit bus with its busy flag read back, the subject of circuits/lcd1602-demo.json: the
// initialisation by instruction of the datasheet, a heart defined in CGRAM, "Hello, world!" on the first row, then the seconds
// since the start on the second one. D12 goes high once the first screen is written.
// Wiring: RS D8 (PB0), R/W D9 (PB1), E D10 (PB2), D4-D7 on D4-D7 (PD4-PD7); D0-D3 unconnected.
#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

#define RS   PB0
#define RW   PB1
#define E    PB2
#define DONE PB4
#define BUS  0xF0u  // PD4-PD7

static void pulse(void) {
    PORTB |= _BV(E);
    _delay_us(1);  // PW_EH: at least 230 ns
    PORTB &= ~_BV(E);
    _delay_us(1);
}

static void writeNibble(uint8_t high) {
    PORTD = (uint8_t)((PORTD & ~BUS) | (high & BUS));
    pulse();
}

static uint8_t readNibble(void) {
    PORTB |= _BV(E);
    _delay_us(1);  // t_DDR: the module drives the bus at most 360 ns after E rises
    const uint8_t value = PIND & BUS;
    PORTB &= ~_BV(E);
    _delay_us(1);
    return value;
}

/// Waits for the busy flag to clear: R/W high, D4-D7 as inputs, two pulses per read of the flag and the address counter.
static void waitReady(void) {
    DDRD &= ~BUS;
    PORTD &= ~BUS;  // No pull-ups: the module drives the bus
    PORTB &= ~_BV(RS);
    PORTB |= _BV(RW);
    uint8_t high;
    do {
        high = readNibble();
        (void)readNibble();
    } while (high & 0x80u);
    PORTB &= ~_BV(RW);
    DDRD |= BUS;
}

static void send(uint8_t value, uint8_t data) {
    waitReady();
    if (data)
        PORTB |= _BV(RS);
    else
        PORTB &= ~_BV(RS);
    writeNibble(value);
    writeNibble((uint8_t)(value << 4));
}

static void command(uint8_t value) {
    send(value, 0);
}

static void print(const char* text) {
    while (*text)
        send((uint8_t)*text++, 1);
}

static const uint8_t heart[8] = {0x00, 0x0A, 0x1F, 0x1F, 0x0E, 0x04, 0x00, 0x00};

int main(void) {
    DDRB = _BV(RS) | _BV(RW) | _BV(E) | _BV(DONE);
    DDRD = BUS;
    // Initialisation by instruction (HD44780U datasheet, figure 24): the busy flag cannot be read before the interface is
    // known, so the first function sets are timed.
    _delay_ms(50);
    writeNibble(0x30);
    _delay_ms(5);
    writeNibble(0x30);
    _delay_us(150);
    writeNibble(0x30);
    _delay_us(50);
    writeNibble(0x20);  // 4-bit interface from now on
    command(0x28);      // Function set: 4-bit, 2 lines, 5 x 8 dots
    command(0x08);      // Display off
    command(0x01);      // Clear
    command(0x06);      // Entry mode: increment, no shift
    command(0x40);      // CGRAM address 0: character 0
    for (uint8_t i = 0; i < sizeof heart; ++i)
        send(heart[i], 1);
    command(0x80);  // DDRAM address 0
    print("Hello, world! ");
    send(0, 1);     // The heart
    command(0x0C);  // Display on, no cursor
    command(0xC0);  // Second row
    print("Seconds: 0");
    PORTB |= _BV(DONE);

    for (uint16_t seconds = 1;; ++seconds) {
        _delay_ms(1000);
        char text[6];
        uint8_t length = 0;
        for (uint16_t rest = seconds; rest; rest /= 10)
            text[length++] = (char)('0' + rest % 10);
        command(0xC0 + 9);
        while (length)
            send((uint8_t)text[--length], 1);
    }
}
