// A KMRTM28028 module (ILI9341, 240 x 320) on the hardware SPI, the subject of circuits/tft-spi-demo.json: initialises the
// controller, then paints a 120 x 80 window at (60, 120) in four colour quadrants, 16 bits per pixel, and raises D12 when
// the last pixel is written. Wiring: D8 RESET, D9 D/C, D10 CS, D11 SDI, D13 SCK.
#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

#define RESET PB0
#define DC    PB1
#define CS    PB2
#define DONE  PB4

#define RED   0xF800u
#define GREEN 0x07E0u
#define BLUE  0x001Fu
#define WHITE 0xFFFFu

#define LEFT   60u
#define TOP    120u
#define WIDTH  120u
#define HEIGHT 80u

static void send(uint8_t value) {
    SPDR = value;
    while (!(SPSR & _BV(SPIF)))
        ;
}

static void command(uint8_t value) {
    PORTB &= ~_BV(DC);
    PORTB &= ~_BV(CS);
    send(value);
    PORTB |= _BV(DC);
}

static void end(void) {
    PORTB |= _BV(CS);
}

static void command1(uint8_t value, uint8_t parameter) {
    command(value);
    send(parameter);
    end();
}

static void window(uint8_t command_code, uint16_t first, uint16_t last) {
    command(command_code);
    send((uint8_t)(first >> 8));
    send((uint8_t)first);
    send((uint8_t)(last >> 8));
    send((uint8_t)last);
    end();
}

static uint16_t color(uint16_t x, uint16_t y) {
    const uint8_t right  = x >= LEFT + WIDTH / 2;
    const uint8_t bottom = y >= TOP + HEIGHT / 2;
    return right ? (bottom ? WHITE : GREEN) : (bottom ? BLUE : RED);
}

int main(void) {
    DDRB  = _BV(RESET) | _BV(DC) | _BV(CS) | _BV(PB3) | _BV(PB5) | _BV(DONE);
    PORTB = _BV(CS) | _BV(RESET);
    SPCR  = _BV(SPE) | _BV(MSTR);  // Mode 0, fck / 2 with SPI2X: 8 MHz
    SPSR  = _BV(SPI2X);

    PORTB &= ~_BV(RESET);  // Hardware reset
    _delay_ms(2);
    PORTB |= _BV(RESET);
    _delay_ms(5);

    command(0x01);  // Software reset
    end();
    _delay_ms(5);
    command(0x11);  // Sleep out
    end();
    _delay_ms(120);
    command1(0x3A, 0x55);  // 16 bits per pixel
    command1(0x36, 0x08);  // Memory access control: BGR panel
    command(0x29);         // Display on
    end();
    window(0x2A, LEFT, LEFT + WIDTH - 1);
    window(0x2B, TOP, TOP + HEIGHT - 1);
    command(0x2C);  // Memory write
    for (uint16_t y = TOP; y < TOP + HEIGHT; ++y)
        for (uint16_t x = LEFT; x < LEFT + WIDTH; ++x) {
            const uint16_t pixel = color(x, y);
            send((uint8_t)(pixel >> 8));
            send((uint8_t)pixel);
        }
    end();
    PORTB |= _BV(DONE);
    for (;;)
        ;
}
