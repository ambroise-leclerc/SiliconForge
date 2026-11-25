// An LCD1602 behind an I2C backpack (PCF8574 at 0x27), driven by the hardware TWI at 100 kHz, the subject of
// circuits/lcd1602-i2c-demo.json: P0 RS, P1 R/W, P2 E, P3 backlight, P4-P7 D4-D7. The busy flag is read back through the
// PCF8574 (a write that raises E with D4-D7 released, then a read after a repeated START). "Hello over I2C!" on the first
// row, the seconds since the start on the second one; D12 goes high once the first screen is written, D13 if the bus ever
// answers with an unexpected status.
// Wiring: SDA A4 (PC4), SCL A5 (PC5).
#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

#define ADDRESS 0x27
#define RS      0x01
#define RW      0x02
#define E       0x04
#define BL      0x08
#define DONE    PB4
#define FAULT   PB5

static void fail(void) {
    PORTB |= _BV(FAULT);
    for (;;) {
    }
}

static uint8_t status(void) {
    return TWSR & 0xF8;
}

static void await(void) {
    while (!(TWCR & _BV(TWINT))) {
    }
}

/// START (or repeated START), then the address with the direction; `expected` is the status of the acknowledged address.
static void begin(uint8_t direction, uint8_t expected) {
    TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
    await();
    if (status() != 0x08 && status() != 0x10)
        fail();
    TWDR = (uint8_t)(ADDRESS << 1 | direction);
    TWCR = _BV(TWINT) | _BV(TWEN);
    await();
    if (status() != expected)
        fail();
}

static void end(void) {
    TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN);
    while (TWCR & _BV(TWSTO)) {
    }
}

static void put(uint8_t value) {
    TWDR = value;
    TWCR = _BV(TWINT) | _BV(TWEN);
    await();
    if (status() != 0x28)
        fail();
}

/// One byte from the PCF8574, not acknowledged: the last of the transfer.
static uint8_t take(void) {
    TWCR = _BV(TWINT) | _BV(TWEN);
    await();
    if (status() != 0x58)
        fail();
    return TWDR;
}

/// A nibble to the LCD: E raised, then lowered, in one transfer of two bytes.
static void writeNibble(uint8_t high, uint8_t flags) {
    begin(0, 0x18);
    put((uint8_t)(high | flags | BL | E));
    put((uint8_t)(high | flags | BL));
    end();
}

/// A nibble from the LCD: E raised with R/W high and D4-D7 released, the port read after a repeated START, E lowered.
static uint8_t readNibble(void) {
    begin(0, 0x18);
    put(0xF0 | RW | BL | E);
    begin(1, 0x40);
    const uint8_t value = take();
    end();
    begin(0, 0x18);
    put(0xF0 | RW | BL);
    end();
    return value & 0xF0;
}

static void waitReady(void) {
    uint8_t high;
    do {
        high = readNibble();
        (void)readNibble();
    } while (high & 0x80);
}

static void send(uint8_t value, uint8_t flags) {
    waitReady();
    writeNibble(value & 0xF0, flags);
    writeNibble((uint8_t)(value << 4), flags);
}

static void print(const char* text) {
    while (*text)
        send((uint8_t)*text++, RS);
}

int main(void) {
    DDRB = _BV(DONE) | _BV(FAULT);
    TWSR = 0;   // Prescaler 1
    TWBR = 72;  // 16 MHz / (16 + 2 * 72) = 100 kHz
    _delay_ms(50);
    // Initialisation by instruction: the interface is not known yet, so the first function sets are timed.
    writeNibble(0x30, 0);
    _delay_ms(5);
    writeNibble(0x30, 0);
    _delay_us(150);
    writeNibble(0x30, 0);
    _delay_us(100);
    writeNibble(0x20, 0);  // 4-bit interface from now on
    send(0x28, 0);         // 2 lines, 5 x 8 dots
    send(0x08, 0);         // Display off
    send(0x01, 0);         // Clear
    send(0x06, 0);         // Increment, no shift
    send(0x0C, 0);         // Display on
    print("Hello over I2C!");
    send(0xC0, 0);
    print("Seconds: 0");
    PORTB |= _BV(DONE);

    for (uint16_t seconds = 1;; ++seconds) {
        _delay_ms(1000);
        char    text[6];
        uint8_t length = 0;
        for (uint16_t rest = seconds; rest; rest /= 10)
            text[length++] = (char)('0' + rest % 10);
        send(0xC0 + 9, 0);
        while (length)
            send((uint8_t)text[--length], RS);
    }
}
