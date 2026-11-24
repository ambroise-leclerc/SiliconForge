#pragma once

#include <siliconforge/core/Types.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace siliconforge::circuit {

/// What the bus of a display carried, one entry per command and the data that followed it (#64).
struct BusTransaction {
    Cycles        at{0};          ///< Cycle of the eighth clock edge of the command byte.
    std::uint8_t  command{0};
    Cycles        bitCycles{0};   ///< Cycles between two clock edges of the command byte: the SPI rate the firmware used.
    std::size_t   dataBytes{0};   ///< Data bytes up to the next command.
    std::uint64_t checksum{0};    ///< FNV-1a of those data bytes, to check a long stream without keeping it.
    std::vector<std::uint8_t> head;  ///< The first of them.
};

/// How a picture differs from the expected one: compared as the controller stores colours, in 5-6-5 bits per channel.
struct ImageDifference {
    std::size_t differing{0};   ///< Pixels of different colour.
    std::size_t firstX{0}, firstY{0};  ///< Where the first of them is, when there is one.
};
/// Compares two pictures of `width` pixels per row, as 0xRRGGBB. A size that differs counts every pixel as different.
[[nodiscard]] ImageDifference compareImages(const std::vector<std::uint32_t>& actual, const std::vector<std::uint32_t>& expected, unsigned width);

/// A TFT module with an ILI9341 (240 x 320, the KMRTM28028) or an ILI9481 (320 x 480, the MCUFRIEND 3.5" shield) controller,
/// seen from the wires (#64). The two share the commands that matter for the pixels, so they share this model; they differ
/// by their size and by the bus a module exposes: 4-wire SPI or an 8-bit parallel bus.
///
/// SPI is mode 0: SDI is sampled at each rising edge of SCK while CS is low, most significant bit first, and D/C is read
/// at the eighth edge. The parallel bus is the 8080 write cycle: the byte on D0-D7 is taken at the rising edge of WR
/// while CS is low, RS low for a command and high for a parameter or pixel data. Modelled: the commands that set the
/// window (CASET, PASET), write the memory (RAMWR, RAMWRC) with the memory access control (MADCTL: MY, MX, MV, and BGR, which these BGR panels need set for red to show as red) and
/// the pixel format (COLMOD: 16 or 18 bits per pixel), sleep, display on and off, inversion, and software and hardware
/// reset. The commands of the usual initialisation sequences (power, timing, gamma) are accepted and counted without
/// effect on the pixels. A command that reads the controller, and a read cycle of the parallel bus, are counted as
/// unsupported: the module does not drive the bus back. The scan direction of the panel (ML, MH), partial and scroll
/// modes are not modelled: the image is the memory.
class Tft {
public:
    enum class Controller { Ili9341, Ili9481 };

    explicit Tft(Controller controller = Controller::Ili9341);

    [[nodiscard]] Controller controller() const noexcept { return kind; }
    [[nodiscard]] unsigned   width() const noexcept { return panelWidth; }
    [[nodiscard]] unsigned   height() const noexcept { return panelHeight; }

    /// Supply on or off. Off, the controller forgets everything and the memory is black.
    void power(bool powerOn);
    /// The state of the LED pin of the module: with the backlight off the panel is black whatever the memory holds. On by
    /// default (a module without the pin, or a controller used alone).
    void backlight(bool lightOn);
    [[nodiscard]] bool backlit() const noexcept { return lit; }
    /// SPI: the levels of the wires now, at cycle `at`; each edge since the previous call is acted on, in the order CS,
    /// RESET, SCK. Call it every time a wire may have changed.
    void spi(bool cs, bool dc, bool clock, bool data, bool reset, Cycles at);
    /// 8080 parallel bus: the same for CS, RS, WR, RD, RESET and the byte on D0-D7.
    void parallel(bool cs, bool rs, bool write, bool read, bool reset, std::uint8_t data, Cycles at);

    /// The memory, row by row from the top-left of the panel, as 0xRRGGBB (what the controller received, with the 5 or 6
    /// bits of each channel expanded to 8).
    [[nodiscard]] const std::vector<std::uint32_t>& memory() const noexcept { return pixels; }
    /// What the panel shows: the memory with inversion applied, black when the display is off or asleep, when the module is
    /// not powered or when its backlight is off.
    [[nodiscard]] std::vector<std::uint32_t> shown() const;
    [[nodiscard]] bool displayOn() const noexcept { return on && !asleep; }
    [[nodiscard]] bool sleeping() const noexcept { return asleep; }
    [[nodiscard]] bool inverted() const noexcept { return inversion; }
    [[nodiscard]] std::uint8_t accessControl() const noexcept { return madctl; }
    [[nodiscard]] std::uint8_t pixelFormat() const noexcept { return colmod; }
    [[nodiscard]] bool powered() const noexcept { return supplied; }

    [[nodiscard]] const std::deque<BusTransaction>& transactions() const noexcept { return log; }
    [[nodiscard]] std::size_t dropped() const noexcept { return lost; }  ///< Transactions forgotten beyond the last 4096.
    [[nodiscard]] std::size_t commands() const noexcept { return commandCount; }
    [[nodiscard]] std::size_t dataBytes() const noexcept { return dataCount; }
    [[nodiscard]] std::size_t pixelsWritten() const noexcept { return written; }
    [[nodiscard]] std::size_t unknown() const noexcept { return unknownCount; }          ///< Commands this controller does not have.
    [[nodiscard]] std::size_t unsupported() const noexcept { return unsupportedCount; }  ///< Reads, which this model cannot answer.
    [[nodiscard]] std::size_t framing() const noexcept { return framingCount; }          ///< CS released in the middle of a byte.
    /// Increases whenever the memory or what the panel shows changes, so that a view redraws only then.
    [[nodiscard]] std::size_t revision() const noexcept { return changes; }

private:
    void edge(bool data, bool dc, Cycles at);
    void hardwareReset(bool reset);
    void byte(std::uint8_t value, bool dc, Cycles at, Cycles firstEdge);
    void command(std::uint8_t value, Cycles at, Cycles bitCycles);
    void parameter(std::uint8_t value);
    void pixel(std::uint8_t value);
    void put(std::uint32_t color);
    void resetController();
    void clearMemory();

    Controller    kind;
    unsigned      panelWidth, panelHeight;
    bool          supplied{false};
    bool          lit{true};
    bool          lastCs{true}, lastClock{false}, lastReset{true}, lastRead{true};
    unsigned      bits{0};
    std::uint8_t  shift{0};
    Cycles        firstEdge{0};

    // Controller.
    bool          on{false}, asleep{true}, inversion{false};
    std::uint8_t  madctl{0}, colmod{0x66};
    std::uint16_t columnStart{0}, columnEnd{0}, rowStart{0}, rowEnd{0};
    std::uint16_t column{0}, row{0};
    std::uint8_t  current{0};
    bool          writing{false};
    static constexpr std::size_t maxParameters = 4;  // The longest parameter list of the commands that are modelled
    std::vector<std::uint8_t> parameters;
    std::uint8_t  partial[3]{};
    unsigned      partialCount{0};

    std::vector<std::uint32_t> pixels;
    std::deque<BusTransaction> log;
    std::size_t                lost{0}, commandCount{0}, dataCount{0}, written{0}, unknownCount{0}, unsupportedCount{0}, framingCount{0}, changes{0};
};

}  // namespace siliconforge::circuit
