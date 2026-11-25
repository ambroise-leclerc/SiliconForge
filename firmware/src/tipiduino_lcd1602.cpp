// Demo of the LCD1602 library of TiPiDuino (avr/libraries/LCD1602), the subject of circuits/lcd1602-tipiduino.json.
// The library drives the module in 4-bit mode, write only (R/W tied to GND), with fixed delays instead of the busy flag.
// Wiring, as in TiPiDuino's LCDTest sketch: RS D2, EN D3, D4-D7 on D9-D12; D0-D3 unconnected.
// The demo: a splash screen with characters defined in CGRAM, a progress bar drawn with them, then the console mode of the
// library (PrintStream, '\n' scrolls the second row up) counting forever. D13 goes high once the splash screen is written.
//
// Built against a TiPiDuino checkout (TIPIDUINO, default ~/devel/TiPiDuino):
//   avr-g++ -mmcu=atmega328p -DF_CPU=16000000UL -Os -std=gnu++17 -fno-exceptions -fno-threadsafe-statics
//     -I$TIPIDUINO/avr/libraries -I$TIPIDUINO/avr/libraries/AvrTL
//     src/tipiduino_lcd1602.cpp $TIPIDUINO/avr/libraries/BasicIO/PrintStream.cpp -o build/tipiduino_lcd1602.elf
#include <AvrTL/AvrTLPin.h>
#include <AvrTL/timer.h>
#include <BasicIO/PrintStream.h>
#include <LCD1602/LCD1602.h>

using namespace avrtl;

LCD1602<2, 3, 9, 10, 11, 12> lcd;  // RS, EN, D4, D5, D6, D7
PrintStream cout;
static StaticPin<13> done;

// CGRAM: codes 1-5 are bars of 1 to 5 columns, 6 a heart, 7 a smiley (code 0 would end a C string).
static uint8_t bar[5][8];
static uint8_t heart[8] = {0x00, 0x0A, 0x1F, 0x1F, 0x0E, 0x04, 0x00, 0x00};
static uint8_t smiley[8] = {0x00, 0x0A, 0x0A, 0x00, 0x11, 0x0E, 0x00, 0x00};

static void print(uint8_t col, uint8_t row, const char* text) {
    lcd.setCursor(col, row);
    while (*text) lcd.sendByte(static_cast<uint8_t>(*text++));
}

static void defineCharacters() {
    for (uint8_t n = 0; n < 5; ++n) {
        const uint8_t row = static_cast<uint8_t>(0x1F << (4 - n)) & 0x1F;
        for (uint8_t line = 0; line < 8; ++line) bar[n][line] = row;
        lcd.createChar(n + 1, bar[n]);
    }
    lcd.createChar(6, heart);
    lcd.createChar(7, smiley);
}

static void splash() {
    lcd.clear();
    print(0, 0, "\x06 TiPiDuino \x06");
    print(0, 1, "LCD1602 + AvrTL\x07");
}

/// A bar of 16 cells x 5 columns, filled one column at a time.
static void progressBar() {
    lcd.clear();
    print(0, 0, "Loading...");
    for (uint8_t columns = 1; columns <= 16 * 5; ++columns) {
        lcd.setCursor(0, 1);
        for (uint8_t cell = 0; cell < 16; ++cell) {
            const int16_t fill = static_cast<int16_t>(columns) - cell * 5;
            lcd.sendByte(fill >= 5 ? 5 : fill > 0 ? static_cast<uint8_t>(fill) : ' ');
        }
        delay(25);
    }
}

int main() {
    done.SetOutput();
    done = 0;
    delay(50);  // power-on: the HD44780 needs more than 40 ms after VDD rises before the first instruction
    lcd.begin();
    defineCharacters();
    splash();
    done = 1;
    delay(2000);

    lcd.blink();  // shows the blinking block cursor while counting
    progressBar();

    lcd.clear();
    cout.begin(&lcd);
    for (uint16_t counter = 0;; ++counter) {
        cout << "Counter=" << counter << '\n';
        delay(1000);
    }
}
