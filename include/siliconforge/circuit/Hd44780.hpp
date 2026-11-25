#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace siliconforge::circuit {

/// A character LCD module with an HD44780 controller (or a compatible one: KS0066, SPLC780...), seen from the wires: the
/// LCD1602 has 16 columns and 2 rows of 5 x 8 dots.
///
/// The bus is the 6800 one: RS selects the instruction register (low) or the data register (high), R/W the direction, and
/// the controller takes D0-D7 at the falling edge of E. After a function set with DL clear, only D4-D7 are used: each byte
/// is two transfers, the high nibble first. With R/W high, the module drives the bus while E is high: the busy flag and the
/// address counter (RS low) or the byte at the address counter (RS high); in 4-bit mode the first E pulse gives the high
/// nibble, the second the low one.
///
/// The instructions are those of the datasheet: clear, home, entry mode, display control, cursor or display shift, function
/// set, CGRAM and DDRAM address; the eight characters of CGRAM can be defined and shown. An instruction takes 37 µs, a clear
/// or a return home 1.52 ms, and the internal reset at power on 10 ms: the busy flag is set meanwhile. A write that arrives
/// while the controller is busy is lost, as on the chip, and counted (early()); so is an E pulse shorter than 230 ns or
/// closer than 500 ns to the previous one (timing()), which is executed all the same. The characters come from the
/// character generator ROM A00 (ASCII, katakana and symbols).
class Hd44780 {
public:
    explicit Hd44780(unsigned columns = 16, unsigned rows = 2);

    [[nodiscard]] unsigned columns() const noexcept { return width; }
    [[nodiscard]] unsigned rows() const noexcept { return height; }

    /// Supply on or off at `seconds`. Powered, the controller resets itself and is busy for 10 ms; off, it forgets all.
    void power(bool powerOn, double seconds);
    [[nodiscard]] bool powered() const noexcept { return supplied; }
    /// The levels of RS, R/W, E and D0-D7 now; an edge of E since the previous call is acted on. Call it every time a wire
    /// may have changed.
    void bus(bool rs, bool rw, bool enable, std::uint8_t data, double seconds);

    /// What the module drives on the bus: the levels of the bits of `mask` (D4-D7 in 4-bit mode, D0-D7 in 8-bit mode).
    struct Output {
        std::uint8_t value{0};
        std::uint8_t mask{0};
    };
    /// What the module drives now: a read cycle (R/W and E high), nothing otherwise.
    [[nodiscard]] std::optional<Output> output() const noexcept { return driving; }

    // The state of the controller.
    [[nodiscard]] bool displayOn() const noexcept { return on; }
    [[nodiscard]] bool cursorOn() const noexcept { return cursor; }
    [[nodiscard]] bool blinkOn() const noexcept { return blink; }
    [[nodiscard]] bool eightBit() const noexcept { return dataLength8; }
    [[nodiscard]] bool twoLines() const noexcept { return lines2; }
    [[nodiscard]] bool busy(double seconds) const noexcept { return seconds < busyUntil; }
    [[nodiscard]] std::uint8_t addressCounter() const noexcept { return address; }
    [[nodiscard]] bool addressingCgram() const noexcept { return cgram; }
    [[nodiscard]] int shift() const noexcept { return displayShift; }

    /// The character codes in DDRAM, by physical position: 80 of them, the second line from 40 in 2-line mode.
    [[nodiscard]] const std::array<std::uint8_t, 80>& ddram() const noexcept { return characters; }
    [[nodiscard]] const std::array<std::uint8_t, 64>& cgramBytes() const noexcept { return patterns; }

    /// The code of the character shown at a cell of the panel, nullopt when that row is not shown (1-line mode).
    [[nodiscard]] std::optional<std::uint8_t> codeAt(unsigned column, unsigned row) const;
    /// The cell the cursor is in, when it is on a shown cell (whether or not the cursor is displayed).
    [[nodiscard]] std::optional<std::pair<unsigned, unsigned>> cursorCell() const;
    /// The eight rows of dots of a character (five bits each, the leftmost dot in bit 4): from the ROM, or from CGRAM for
    /// the codes 0 to 15.
    [[nodiscard]] std::array<std::uint8_t, 8> glyph(std::uint8_t code) const;
    /// What the panel shows, row by row, as UTF-8: the codes of the ROM as the characters they look like, a character of
    /// CGRAM as U+0000 to U+0007. Blank (spaces) when the display is off or the module not powered; the cursor is left out.
    [[nodiscard]] std::vector<std::string> text() const;

    [[nodiscard]] std::size_t instructions() const noexcept { return instructionCount; }
    [[nodiscard]] std::size_t dataWrites() const noexcept { return writeCount; }
    [[nodiscard]] std::size_t reads() const noexcept { return readCount; }
    [[nodiscard]] std::size_t early() const noexcept { return earlyCount; }    ///< Writes lost because the controller was busy.
    [[nodiscard]] std::size_t timing() const noexcept { return timingCount; }  ///< E pulses too short or too close.
    /// Increases whenever what the panel shows may have changed, so that a view redraws only then.
    [[nodiscard]] std::size_t revision() const noexcept { return changes; }

    /// The character a code of ROM A00 looks like, as UTF-8 ("" for a code without a character, shown blank).
    [[nodiscard]] static std::string character(std::uint8_t code);

private:
    void reset(double seconds);
    void rebase(double seconds);
    void transfer(bool rs, bool rw, std::uint8_t data, double seconds);
    void write(bool rs, std::uint8_t value, double seconds);
    void instruction(std::uint8_t value, double seconds);
    [[nodiscard]] std::uint8_t readValue(bool rs, double seconds) const;
    void advance();
    void moveDisplay(int by);
    [[nodiscard]] std::optional<std::size_t> physical(std::uint8_t ddramAddress) const;

    unsigned width, height;
    bool     supplied{false};
    bool     lastEnable{false};
    double   risingAt{-1}, previousRising{-1};
    double   lastTime{0};  ///< The latest instant seen: an earlier one means the clock of the MCU started again.
    std::optional<Output> driving;

    // Controller.
    bool          dataLength8{true}, lines2{false}, font5x10{false};
    bool          on{false}, cursor{false}, blink{false};
    bool          increment{true}, shiftOnWrite{false};
    bool          cgram{false};
    std::uint8_t  address{0};
    int           displayShift{0};
    bool          lowNibble{false};  ///< 4-bit mode: the next transfer is the low nibble.
    std::uint8_t  pending{0};        ///< 4-bit mode: the high nibble received.
    std::uint8_t  readLatch{0};      ///< 4-bit mode: the byte whose nibbles a read gives.
    double        busyUntil{0};

    std::array<std::uint8_t, 80> characters{};
    std::array<std::uint8_t, 64> patterns{};
    std::size_t instructionCount{0}, writeCount{0}, readCount{0}, earlyCount{0}, timingCount{0}, changes{0};
};

}  // namespace siliconforge::circuit
