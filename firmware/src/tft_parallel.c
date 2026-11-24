// A MCUFRIEND 3.5" shield (ILI9481, 320 x 480) on its 8-bit parallel bus, the subject of circuits/tft-parallel-demo.json:
// the usual initialisation sequence, then a 120 x 80 window at (100, 200) in four colour quadrants, 16 bits per pixel;
// D12 goes high when the last pixel is written. Wiring as on the Uno shield: RD A0, WR A1, RS A2, CS A3, RESET A4,
// D0-D1 on D8-D9 (PB0-PB1), D2-D7 on D2-D7 (PD2-PD7).
#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

#define RD    PC0
#define WR    PC1
#define RS    PC2
#define CS    PC3
#define RESET PC4
#define DONE  PB4

#define RED   0xF800u
#define GREEN 0x07E0u
#define BLUE  0x001Fu
#define WHITE 0xFFFFu

#define LEFT   100u
#define TOP    200u
#define WIDTH  120u
#define HEIGHT 80u

static void write8(uint8_t value) {
    PORTB = (uint8_t)((PORTB & ~0x03u) | (value & 0x03u));
    PORTD = (uint8_t)((PORTD & 0x03u) | (value & 0xFCu));
    PORTC &= ~_BV(WR);
    PORTC |= _BV(WR);
}

static void command(uint8_t value) {
    PORTC &= ~_BV(CS);
    PORTC &= ~_BV(RS);
    write8(value);
    PORTC |= _BV(RS);
}

static void end(void) {
    PORTC |= _BV(CS);
}

static void send(uint8_t value) {
    write8(value);
}

static void withData(uint8_t code, const uint8_t* data, uint8_t count) {
    command(code);
    for (uint8_t i = 0; i < count; ++i)
        send(data[i]);
    end();
}

static void window(uint8_t code, uint16_t first, uint16_t last) {
    const uint8_t data[4] = {(uint8_t)(first >> 8), (uint8_t)first, (uint8_t)(last >> 8), (uint8_t)last};
    withData(code, data, 4);
}

static uint16_t color(uint16_t x, uint16_t y) {
    const uint8_t right  = x >= LEFT + WIDTH / 2;
    const uint8_t bottom = y >= TOP + HEIGHT / 2;
    return right ? (bottom ? WHITE : GREEN) : (bottom ? BLUE : RED);
}

int main(void) {
    DDRB  = _BV(PB0) | _BV(PB1) | _BV(DONE);
    DDRD  = 0xFC;
    DDRC  = _BV(RD) | _BV(WR) | _BV(RS) | _BV(CS) | _BV(RESET);
    PORTC = _BV(RD) | _BV(WR) | _BV(CS) | _BV(RESET);

    PORTC &= ~_BV(RESET);  // Hardware reset
    _delay_ms(2);
    PORTC |= _BV(RESET);
    _delay_ms(5);

    command(0x01);  // Software reset
    end();
    _delay_ms(5);
    command(0x11);  // Sleep out
    end();
    _delay_ms(20);
    static const uint8_t power[]  = {0x07, 0x42, 0x18};
    static const uint8_t vcom[]   = {0x00, 0x07, 0x10};
    static const uint8_t normal[] = {0x01, 0x02};
    static const uint8_t panel[]  = {0x10, 0x3B, 0x00, 0x02, 0x11};
    static const uint8_t rate[]   = {0x03};
    withData(0xD0, power, sizeof power);
    withData(0xD1, vcom, sizeof vcom);
    withData(0xD2, normal, sizeof normal);
    withData(0xC0, panel, sizeof panel);
    withData(0xC5, rate, sizeof rate);
    static const uint8_t format[] = {0x55};  // 16 bits per pixel
    static const uint8_t access[] = {0x08};  // Memory access control: BGR panel
    withData(0x3A, format, 1);
    withData(0x36, access, 1);
    _delay_ms(120);
    command(0x29);  // Display on
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
