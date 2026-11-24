// tools/captures2gif: the LZW codes, the frames as a viewer composes them, the palette and the scaling. A minimal
// decoder written from the GIF89a specification reads the encoder's output back.
#include <Gif.hpp>

#include <webfront/testing/Bdd.hpp>

#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

using namespace webfront::bdd;

namespace gif = siliconforge::gif;
using Bytes = std::vector<std::uint8_t>;

namespace {

Bytes unlzw(const Bytes& data, int minimumCodeSize) {
    const int clear = 1 << minimumCodeSize;
    const int end = clear + 1;
    std::vector<Bytes> table;
    int codeSize = 0;
    const auto reset = [&] {
        table.assign(static_cast<std::size_t>(clear) + 2, {});
        for (int i = 0; i < clear; ++i)
            table[static_cast<std::size_t>(i)] = {static_cast<std::uint8_t>(i)};
        codeSize = minimumCodeSize + 1;
    };
    reset();
    Bytes out;
    std::size_t bit = 0;
    int previous = -1;
    while (bit + static_cast<std::size_t>(codeSize) <= data.size() * 8) {
        int code = 0;
        for (int i = 0; i < codeSize; ++i, ++bit)
            code |= ((data[bit / 8] >> (bit % 8)) & 1) << i;
        if (code == clear) {
            reset();
            previous = -1;
            continue;
        }
        if (code == end)
            return out;
        Bytes entry;
        if (code < static_cast<int>(table.size()))
            entry = table[static_cast<std::size_t>(code)];
        else if (code == static_cast<int>(table.size()) && previous >= 0) {
            entry = table[static_cast<std::size_t>(previous)];
            entry.push_back(entry.front());
        } else
            throw std::runtime_error("invalid LZW code");
        out.insert(out.end(), entry.begin(), entry.end());
        if (previous >= 0 && table.size() < 4096) {
            Bytes added = table[static_cast<std::size_t>(previous)];
            added.push_back(entry.front());
            table.push_back(std::move(added));
            if (static_cast<int>(table.size()) == (1 << codeSize) && codeSize < 12)
                ++codeSize;
        }
        previous = code;
    }
    throw std::runtime_error("no end code");
}

struct Decoded {
    int width = 0, height = 0, loops = -1;
    std::vector<gif::Color> palette;
    std::vector<Bytes> frames;  // The canvas after each image, as palette indices.
    std::vector<int> delays;
};

Decoded decode(const Bytes& g) {
    std::size_t p = 0;
    const auto byte = [&] { return g.at(p++); };
    const auto shortValue = [&] { const int lo = byte(); return lo | (byte() << 8); };
    Decoded d;
    require(std::string(g.begin(), g.begin() + 6) == "GIF89a");
    p = 6;
    d.width = shortValue();
    d.height = shortValue();
    const int packed = byte();
    p += 2;
    require((packed & 0x80) != 0);
    for (int i = 0; i < (2 << (packed & 7)); ++i)
        d.palette.push_back({byte(), byte(), byte()});
    Bytes canvas(static_cast<std::size_t>(d.width) * d.height, 0);
    int delay = 0, transparent = -1;
    for (;;) {
        const int block = byte();
        if (block == 0x3B)
            return d;
        if (block == 0x21) {
            const int label = byte();
            Bytes payload;
            for (int n = byte(); n; n = byte())
                for (int i = 0; i < n; ++i)
                    payload.push_back(byte());
            if (label == 0xF9) {
                require(((payload[0] >> 2) & 7) == 1);  // Keep the previous frame under the next.
                delay = payload[1] | (payload[2] << 8);
                transparent = (payload[0] & 1) ? payload[3] : -1;
            } else if (label == 0xFF && std::string(payload.begin(), payload.begin() + 11) == "NETSCAPE2.0") {
                d.loops = payload[12] | (payload[13] << 8);
            }
            continue;
        }
        require(block == 0x2C);
        const int left = shortValue(), top = shortValue(), w = shortValue(), h = shortValue();
        require(byte() == 0);
        const int minimumCodeSize = byte();
        Bytes data;
        for (int n = byte(); n; n = byte())
            for (int i = 0; i < n; ++i)
                data.push_back(byte());
        const Bytes pixels = unlzw(data, minimumCodeSize);
        require(pixels.size() == static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const auto index = pixels[static_cast<std::size_t>(y) * w + x];
                if (index != transparent)
                    canvas[static_cast<std::size_t>(top + y) * d.width + left + x] = index;
            }
        d.frames.push_back(canvas);
        d.delays.push_back(delay);
    }
}

}  // namespace

const Scenario gifLzwCodesReadBackAcrossCode{"GIF LZW codes read back, across code widths and dictionary resets", {"gif"}, [] {
    std::mt19937 random(1729);
    for (int minimumCodeSize : {2, 4, 8}) {
        const int alphabet = 1 << minimumCodeSize;
        for (std::size_t length : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{300}, std::size_t{200000}}) {
            Bytes indices(length);
            // Runs and noise: long runs fill the dictionary slowly, noise fills it fast and forces resets.
            for (std::size_t i = 0; i < length; ++i)
                indices[i] = static_cast<std::uint8_t>(i % 1000 < 500 ? (i / 37) % alphabet : random() % alphabet);
            check(unlzw(gif::lzw(indices, minimumCodeSize), minimumCodeSize) == indices);
        }
    }
}};

const Scenario gifFramesComposeBackToTheSequence{"GIF frames compose back to the sequence, repeated ones lengthening the previous", {"gif"}, [] {
    const int width = 7, height = 5;
    const std::vector<gif::Color> palette{{0, 0, 0}, {255, 0, 0}, {0, 255, 0}};
    std::vector<Bytes> frames(4, Bytes(width * height, 0));
    frames[1][8] = 1;
    frames[1][30] = 2;
    frames[2] = frames[1];  // Repeated.
    frames[3] = frames[1];
    frames[3][8] = 0;
    const Decoded d = decode(gif::encode(width, height, palette, frames, 10));
    check(d.width == width);
    check(d.height == height);
    check(d.loops == 0);
    check(d.palette[1] == palette[1]);
    require(d.frames.size() == 3);
    check(d.frames[0] == frames[0]);
    check(d.frames[1] == frames[1]);
    check(d.frames[2] == frames[3]);
    check(d.delays == std::vector<int>{10, 20, 10});
}};

const Scenario gifPaletteKeepsTheColoursOfA{"GIF palette keeps the colours of a few-colour image and maps each pixel to its nearest", {"gif"}, [] {
    gif::Image image{4, 1, {10, 10, 10, 12, 10, 10, 200, 0, 0, 0, 0, 250}};
    const auto exact = gif::medianCut(std::span(&image, 1), 4);
    require(exact.size() == 4);
    const auto indices = gif::quantize(image, exact);
    for (std::size_t i = 0; i < 4; ++i)
        check(exact[indices[i]] == gif::Color{image.rgb[i * 3], image.rgb[i * 3 + 1], image.rgb[i * 3 + 2]});
    const auto distance = [](const gif::Color& a, const gif::Color& b) {
        int sum = 0;
        for (std::size_t c = 0; c < 3; ++c)
            sum += (a[c] - b[c]) * (a[c] - b[c]);
        return sum;
    };
    for (int colors : {1, 2, 3}) {
        const auto palette = gif::medianCut(std::span(&image, 1), colors);
        check(palette.size() == static_cast<std::size_t>(colors));
        const auto mapped = gif::quantize(image, palette);
        for (std::size_t i = 0; i < 4; ++i) {
            const gif::Color pixel{image.rgb[i * 3], image.rgb[i * 3 + 1], image.rgb[i * 3 + 2]};
            for (const auto& other : palette)
                check(distance(pixel, palette[mapped[i]]) <= distance(pixel, other));
        }
    }
}};

const Scenario gifScalingKeepsAnImageOfThe{"GIF scaling keeps an image of the same size and averages a uniform one", {"gif"}, [] {
    gif::Image image{3, 2, {0, 50, 100, 150, 200, 250, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}};
    check(gif::resize(image, 3, 2).rgb == image.rgb);
    gif::Image uniform{8, 8, Bytes(8 * 8 * 3, 77)};
    check(gif::resize(uniform, 3, 5).rgb == Bytes(3 * 5 * 3, 77));
}};
