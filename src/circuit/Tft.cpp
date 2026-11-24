#include <siliconforge/circuit/Tft.hpp>

#include <algorithm>
#include <set>

namespace siliconforge::circuit {
namespace {
constexpr std::size_t maxTransactions = 4096;
constexpr std::size_t headBytes       = 16;

/// Commands that write to the controller and leave the pixels alone, or that this model does not follow.
const std::set<std::uint8_t> accepted{0x00, 0x10, 0x12, 0x13, 0x26, 0x30, 0x33, 0x34, 0x35, 0x37, 0x38, 0x39, 0x51, 0x53, 0x55, 0x57, 0x59,
                                      0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC7, 0xCA, 0xCB,
                                      0xCF, 0xE0, 0xE1, 0xE8, 0xEA, 0xED, 0xEF, 0xF2, 0xF6, 0xF7};
/// Commands that read from the controller.
/// More of the ILI9481, whose initialisation differs from the ILI9341's.
const std::set<std::uint8_t> acceptedByIli9481{0xC6, 0xC8, 0xD0, 0xD1, 0xD2, 0xE4, 0xEE};
/// Commands that read from the controller.
const std::set<std::uint8_t> reads{0x04, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x2E, 0x3E, 0x45, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5B,
                                   0xDA, 0xDB, 0xDC, 0xD3, 0xD9, 0xFB};

std::uint32_t expand(unsigned red, unsigned green, unsigned blue, unsigned redBits, unsigned greenBits, unsigned blueBits) {
    const auto channel = [](unsigned value, unsigned bitCount) {
        const auto top = bitCount == 8 ? value : (value << (8 - bitCount)) | (value >> (2 * bitCount - 8));
        return top & 0xFFu;
    };
    return channel(red, redBits) << 16 | channel(green, greenBits) << 8 | channel(blue, blueBits);
}
}  // namespace

Tft::Tft(Controller controller)
    : kind(controller), panelWidth(controller == Controller::Ili9341 ? 240 : 320), panelHeight(controller == Controller::Ili9341 ? 320 : 480),
      columnEnd(static_cast<std::uint16_t>(panelWidth - 1)), rowEnd(static_cast<std::uint16_t>(panelHeight - 1)),
      pixels(static_cast<std::size_t>(panelWidth) * panelHeight, 0) {}

void Tft::clearMemory() {
    std::ranges::fill(pixels, 0u);
    ++changes;
}

void Tft::resetController() {
    on = false;
    asleep = true;
    inversion = false;
    madctl = 0;
    colmod = 0x66;
    columnStart = 0;
    columnEnd   = static_cast<std::uint16_t>(panelWidth - 1);
    rowStart    = 0;
    rowEnd      = static_cast<std::uint16_t>(panelHeight - 1);
    column = row = 0;
    current      = 0;
    writing      = false;
    parameters.clear();
    partialCount = 0;
    ++changes;
}

void Tft::backlight(bool lightOn) {
    if (lightOn == lit)
        return;
    lit = lightOn;
    ++changes;  // What the panel shows changed
}

void Tft::power(bool powerOn) {
    if (powerOn == supplied)
        return;
    supplied = powerOn;
    bits     = 0;
    if (!supplied) {
        resetController();
        clearMemory();
    } else {
        lastCs = true;
        lastClock = false;
        lastReset = true;
        lastRead  = true;
    }
}

void Tft::hardwareReset(bool reset) {
    if (!reset && lastReset) {
        resetController();
        bits = 0;
    }
    lastReset = reset;
}

void Tft::spi(bool cs, bool dc, bool clock, bool data, bool reset, Cycles at) {
    if (!supplied)
        return;
    if (cs && !lastCs && bits != 0) {  // Released in the middle of a byte: what was shifted in is lost.
        ++framingCount;
        bits = 0;
    }
    lastCs = cs;
    hardwareReset(reset);
    if (clock && !lastClock && !cs && reset)
        edge(data, dc, at);
    lastClock = clock;
}

void Tft::parallel(bool cs, bool rs, bool write, bool read, bool reset, std::uint8_t data, Cycles at) {
    if (!supplied)
        return;
    lastCs = cs;
    hardwareReset(reset);
    if (write && !lastClock && !cs && reset)
        byte(data, rs, at, at);
    if (read && !lastRead && !cs && reset)
        ++unsupportedCount;  // A read cycle: the module does not drive the bus.
    lastClock = write;
    lastRead  = read;
}

void Tft::edge(bool data, bool dc, Cycles at) {
    if (bits == 0) {
        shift     = 0;
        firstEdge = at;
    }
    shift = static_cast<std::uint8_t>((shift << 1) | (data ? 1 : 0));
    if (++bits == 8) {
        bits = 0;
        byte(shift, dc, at, firstEdge);
    }
}

void Tft::byte(std::uint8_t value, bool dc, Cycles at, Cycles first) {
    if (!dc) {
        command(value, at, (at - first) / 7);
        return;
    }
    ++dataCount;
    if (!log.empty()) {
        auto& last = log.back();
        ++last.dataBytes;
        last.checksum = (last.checksum ^ value) * 1099511628211ull;
        if (last.head.size() < headBytes)
            last.head.push_back(value);
    }
    if (writing)
        pixel(value);
    else
        parameter(value);
}

void Tft::command(std::uint8_t value, Cycles at, Cycles bitCycles) {
    ++commandCount;
    log.push_back({at, value, bitCycles, 0, 1469598103934665603ull, {}});
    if (log.size() > maxTransactions) {
        log.pop_front();
        ++lost;
    }
    current = value;
    parameters.clear();
    writing      = false;
    partialCount = 0;
    switch (value) {
        case 0x01:  // Software reset: the memory keeps its content.
            resetController();
            break;
        case 0x11: asleep = false; ++changes; break;
        case 0x10: asleep = true; ++changes; break;
        case 0x20: inversion = false; ++changes; break;
        case 0x21: inversion = true; ++changes; break;
        case 0x28: on = false; ++changes; break;
        case 0x29: on = true; ++changes; break;
        case 0x2C:
            column  = columnStart;
            row     = rowStart;
            writing = true;
            break;
        case 0x3C: writing = true; break;  // Continues where the last write stopped.
        case 0x2A: case 0x2B: case 0x36: case 0x3A: break;
        default:
            if (reads.contains(value))
                ++unsupportedCount;
            else if (!accepted.contains(value) && !(kind == Controller::Ili9481 && acceptedByIli9481.contains(value)))
                ++unknownCount;
    }
}

void Tft::parameter(std::uint8_t value) {
    // No command takes more than four parameters: what follows is ignored, whatever the command (and not kept).
    if (parameters.size() >= maxParameters)
        return;
    parameters.push_back(value);
    switch (current) {
        case 0x2A:
            if (parameters.size() == 4) {
                columnStart = static_cast<std::uint16_t>(parameters[0] << 8 | parameters[1]);
                columnEnd   = static_cast<std::uint16_t>(parameters[2] << 8 | parameters[3]);
            }
            break;
        case 0x2B:
            if (parameters.size() == 4) {
                rowStart = static_cast<std::uint16_t>(parameters[0] << 8 | parameters[1]);
                rowEnd   = static_cast<std::uint16_t>(parameters[2] << 8 | parameters[3]);
            }
            break;
        case 0x36:
            if (parameters.size() == 1)
                madctl = value;
            break;
        case 0x3A:
            if (parameters.size() == 1)
                colmod = value;
            break;
        default: break;
    }
}

void Tft::pixel(std::uint8_t value) {
    partial[partialCount++] = value;
    const auto depth = colmod & 0x07;
    // These panels have their colour filters in blue-green-red order: the controller compensates when BGR is set, so that
    // a word whose high bits are red shows red. Left clear, red and blue are exchanged.
    const auto bgr = (madctl & 0x08) == 0;
    if (depth == 0x05) {  // 16 bits: RRRRRGGG GGGBBBBB
        if (partialCount < 2)
            return;
        partialCount = 0;
        const unsigned word = static_cast<unsigned>(partial[0]) << 8 | partial[1];
        const auto     r = word >> 11, g = (word >> 5) & 0x3Fu, b = word & 0x1Fu;
        put(bgr ? expand(b, g, r, 5, 6, 5) : expand(r, g, b, 5, 6, 5));
    } else {  // 18 bits: three bytes of six significant bits, in the upper bits
        if (partialCount < 3)
            return;
        partialCount = 0;
        const auto r = partial[0] >> 2, g = partial[1] >> 2, b = partial[2] >> 2;
        put(bgr ? expand(b, g, r, 6, 6, 6) : expand(r, g, b, 6, 6, 6));
    }
}

void Tft::put(std::uint32_t color) {
    const bool exchange = (madctl & 0x20) != 0, mirrorX = (madctl & 0x40) != 0, mirrorY = (madctl & 0x80) != 0;
    const unsigned columns = exchange ? panelHeight : panelWidth, rows = exchange ? panelWidth : panelHeight;
    if (column < columns && row < rows) {
        const unsigned c = mirrorX ? columns - 1 - column : column;
        const unsigned r = mirrorY ? rows - 1 - row : row;
        const unsigned x = exchange ? r : c, y = exchange ? c : r;
        auto& target = pixels[static_cast<std::size_t>(y) * panelWidth + x];
        if (target != color) {
            target = color;
            ++changes;
        }
    }
    ++written;
    if (++column > columnEnd) {
        column = columnStart;
        if (++row > rowEnd)
            row = rowStart;
    }
}

std::vector<std::uint32_t> Tft::shown() const {
    if (!displayOn() || !supplied || !lit)
        return std::vector<std::uint32_t>(pixels.size(), 0u);
    if (!inversion)
        return pixels;
    auto result = pixels;
    for (auto& pixel : result)
        pixel = ~pixel & 0xFFFFFFu;
    return result;
}

}  // namespace siliconforge::circuit

namespace siliconforge::circuit {
ImageDifference compareImages(const std::vector<std::uint32_t>& actual, const std::vector<std::uint32_t>& expected, unsigned width) {
    ImageDifference result;
    if (actual.size() != expected.size()) {
        result.differing = std::max(actual.size(), expected.size());
        return result;
    }
    // As the controller stores a colour: 5 bits of red, 6 of green, 5 of blue.
    const auto quantised = [](std::uint32_t color) { return (color >> 19 & 0x1Fu) << 11 | (color >> 10 & 0x3Fu) << 5 | (color >> 3 & 0x1Fu); };
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (quantised(actual[i]) != quantised(expected[i])) {
            if (result.differing++ == 0) {
                result.firstX = i % width;
                result.firstY = i / width;
            }
        }
    return result;
}
}  // namespace siliconforge::circuit
