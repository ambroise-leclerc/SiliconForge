#include "Gif.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <ranges>
#include <stdexcept>

namespace siliconforge::gif {
namespace {

double lanczos3(double x) {
    if (x == 0.0)
        return 1.0;
    if (x <= -3.0 || x >= 3.0)
        return 0.0;
    const double px = std::numbers::pi * x;
    return 3.0 * std::sin(px) * std::sin(px / 3.0) / (px * px);
}

/// Contributions of the source pixels to each output pixel along one axis.
struct Weights {
    std::vector<int> first;
    std::vector<std::vector<double>> values;
};

Weights weights(int source, int target) {
    const double scale = static_cast<double>(source) / target;
    const double filterScale = std::max(scale, 1.0);
    const double support = 3.0 * filterScale;
    Weights w;
    for (int x = 0; x < target; ++x) {
        const double center = (x + 0.5) * scale;
        const int lo = std::max(0, static_cast<int>(center - support + 0.5));
        const int hi = std::min(source, static_cast<int>(center + support + 0.5));
        std::vector<double> values;
        double total = 0.0;
        for (int i = lo; i < hi; ++i) {
            values.push_back(lanczos3((i + 0.5 - center) / filterScale));
            total += values.back();
        }
        for (double& v : values)
            v = total != 0.0 ? v / total : 0.0;
        w.first.push_back(lo);
        w.values.push_back(std::move(values));
    }
    return w;
}

std::uint8_t clampByte(double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L)); }

struct Box {
    std::vector<std::pair<Color, std::uint64_t>> colors;  // Each distinct colour and its number of pixels.
    int channel = 0;                                      // The channel of widest range.
    int range = 0;
    std::uint64_t pixels = 0;

    void measure() {
        range = -1;
        pixels = 0;
        for (const auto& [color, count] : colors)
            pixels += count;
        for (int c = 0; c < 3; ++c) {
            const auto [lo, hi] = std::ranges::minmax(colors | std::views::transform([c](const auto& e) { return e.first[c]; }));
            if (hi - lo > range) {
                range = hi - lo;
                channel = c;
            }
        }
    }
};

void putShort(std::vector<std::uint8_t>& out, int value) {
    out.push_back(static_cast<std::uint8_t>(value & 255));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 255));
}

}  // namespace

Image resize(const Image& source, int width, int height) {
    if (width <= 0 || height <= 0 || source.width <= 0 || source.height <= 0)
        throw std::invalid_argument("resize: empty image");
    const auto horizontal = weights(source.width, width);
    const auto vertical = weights(source.height, height);
    std::vector<double> rows(static_cast<std::size_t>(source.height) * width * 3);
    for (int y = 0; y < source.height; ++y)
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c) {
                double sum = 0.0;
                const auto& w = horizontal.values[static_cast<std::size_t>(x)];
                for (std::size_t i = 0; i < w.size(); ++i)
                    sum += w[i] * source.rgb[(static_cast<std::size_t>(y) * source.width + horizontal.first[static_cast<std::size_t>(x)] + i) * 3 + c];
                rows[(static_cast<std::size_t>(y) * width + x) * 3 + c] = sum;
            }
    Image result{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 3)};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c) {
                double sum = 0.0;
                const auto& w = vertical.values[static_cast<std::size_t>(y)];
                for (std::size_t i = 0; i < w.size(); ++i)
                    sum += w[i] * rows[((vertical.first[static_cast<std::size_t>(y)] + i) * width + x) * 3 + c];
                result.rgb[(static_cast<std::size_t>(y) * width + x) * 3 + c] = clampByte(sum);
            }
    return result;
}

std::vector<Color> medianCut(std::span<const Image> images, int colors) {
    if (colors < 1 || colors > 255)
        throw std::invalid_argument("medianCut: between 1 and 255 colours");
    std::map<Color, std::uint64_t> histogram;
    for (const Image& image : images)
        for (std::size_t i = 0; i + 2 < image.rgb.size(); i += 3)
            ++histogram[{image.rgb[i], image.rgb[i + 1], image.rgb[i + 2]}];
    if (histogram.empty())
        throw std::invalid_argument("medianCut: no pixel");
    std::vector<Box> boxes(1);
    boxes[0].colors.assign(histogram.begin(), histogram.end());
    boxes[0].measure();
    while (static_cast<int>(boxes.size()) < colors) {
        // Split the most populated box that still holds several colours, along its widest channel.
        auto box = std::ranges::max_element(boxes, {}, [](const Box& b) {
            return b.range > 0 ? static_cast<double>(b.pixels) : -1.0;
        });
        if (box->range <= 0)
            break;
        const int c = box->channel;
        std::ranges::sort(box->colors, {}, [c](const auto& e) { return e.first[c]; });
        std::uint64_t seen = 0;
        std::size_t cut = 1;
        for (; cut < box->colors.size(); ++cut) {
            seen += box->colors[cut - 1].second;
            if (seen * 2 >= box->pixels)
                break;
        }
        cut = std::min(cut, box->colors.size() - 1);
        Box upper;
        upper.colors.assign(box->colors.begin() + static_cast<std::ptrdiff_t>(cut), box->colors.end());
        box->colors.resize(cut);
        box->measure();
        upper.measure();
        boxes.push_back(std::move(upper));
    }
    std::vector<Color> palette;
    for (const Box& b : boxes) {
        std::array<double, 3> sum{};
        for (const auto& [color, count] : b.colors)
            for (int c = 0; c < 3; ++c)
                sum[static_cast<std::size_t>(c)] += static_cast<double>(color[static_cast<std::size_t>(c)]) * static_cast<double>(count);
        palette.push_back({clampByte(sum[0] / static_cast<double>(b.pixels)), clampByte(sum[1] / static_cast<double>(b.pixels)),
                           clampByte(sum[2] / static_cast<double>(b.pixels))});
    }
    return palette;
}

std::vector<std::uint8_t> quantize(const Image& image, std::span<const Color> palette) {
    std::map<Color, std::uint8_t> nearest;  // Screenshots hold few distinct colours.
    std::vector<std::uint8_t> indices(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t p = 0; p < indices.size(); ++p) {
        const Color color{image.rgb[p * 3], image.rgb[p * 3 + 1], image.rgb[p * 3 + 2]};
        auto found = nearest.find(color);
        if (found == nearest.end()) {
            int best = 0;
            int bestDistance = 1 << 30;
            for (std::size_t i = 0; i < palette.size(); ++i) {
                int distance = 0;
                for (std::size_t c = 0; c < 3; ++c) {
                    const int d = color[c] - palette[i][c];
                    distance += d * d;
                }
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = static_cast<int>(i);
                }
            }
            found = nearest.emplace(color, static_cast<std::uint8_t>(best)).first;
        }
        indices[p] = found->second;
    }
    return indices;
}

std::vector<std::uint8_t> lzw(std::span<const std::uint8_t> indices, int minimumCodeSize) {
    const int clear = 1 << minimumCodeSize;
    const int end = clear + 1;
    const int alphabet = clear;
    std::vector<std::uint16_t> children(4096 * static_cast<std::size_t>(alphabet), 0);  // 0: no child yet.
    std::vector<std::uint8_t> out;
    std::uint32_t buffer = 0;
    int bits = 0;
    int codeSize = minimumCodeSize + 1;
    int next = end + 1;
    const auto put = [&](int code) {
        buffer |= static_cast<std::uint32_t>(code) << bits;
        bits += codeSize;
        while (bits >= 8) {
            out.push_back(static_cast<std::uint8_t>(buffer & 255));
            buffer >>= 8;
            bits -= 8;
        }
    };
    put(clear);
    if (indices.empty()) {
        put(end);
    } else {
        int prefix = indices[0];
        for (std::size_t i = 1; i < indices.size(); ++i) {
            const int k = indices[i];
            if (k >= alphabet)
                throw std::invalid_argument("lzw: index beyond the code size");
            auto& child = children[static_cast<std::size_t>(prefix) * alphabet + k];
            if (child) {
                prefix = child;
                continue;
            }
            put(prefix);
            if (next < 4096) {
                child = static_cast<std::uint16_t>(next);
                if (next == (1 << codeSize))
                    ++codeSize;
                ++next;
            } else {
                put(clear);
                std::ranges::fill(children, std::uint16_t{0});
                codeSize = minimumCodeSize + 1;
                next = end + 1;
            }
            prefix = k;
        }
        put(prefix);
        put(end);
    }
    if (bits > 0)
        out.push_back(static_cast<std::uint8_t>(buffer & 255));
    return out;
}

std::vector<std::uint8_t> encode(int width, int height, std::span<const Color> palette,
                                 std::span<const std::vector<std::uint8_t>> frames, int delayCentiseconds) {
    if (palette.empty() || palette.size() > 255)
        throw std::invalid_argument("encode: between 1 and 255 colours");
    if (frames.empty())
        throw std::invalid_argument("encode: no frame");
    const auto pixels = static_cast<std::size_t>(width) * height;
    for (const auto& frame : frames)
        if (frame.size() != pixels)
            throw std::invalid_argument("encode: frames of different sizes");
    const auto transparent = static_cast<std::uint8_t>(palette.size());
    int tableBits = 1;
    while ((1 << tableBits) < static_cast<int>(palette.size()) + 1)
        ++tableBits;
    const int minimumCodeSize = std::max(2, tableBits);

    // Frames to write: the rectangle that changed, its pixels (unchanged ones transparent) and its display time.
    struct Part {
        int left, top, width, height, delay;
        std::vector<std::uint8_t> indices;
        bool transparency;
    };
    std::vector<Part> parts{{0, 0, width, height, delayCentiseconds, frames[0], false}};
    for (std::size_t f = 1; f < frames.size(); ++f) {
        const auto& previous = frames[f - 1];
        const auto& frame = frames[f];
        int left = width, top = height, right = -1, bottom = -1;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const auto p = static_cast<std::size_t>(y) * width + x;
                if (frame[p] != previous[p]) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        if (right < 0) {
            parts.back().delay += delayCentiseconds;  // A repeated frame lengthens the previous one.
            continue;
        }
        Part part{left, top, right - left + 1, bottom - top + 1, delayCentiseconds, {}, true};
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x) {
                const auto p = static_cast<std::size_t>(y) * width + x;
                part.indices.push_back(frame[p] == previous[p] ? transparent : frame[p]);
            }
        parts.push_back(std::move(part));
    }

    std::vector<std::uint8_t> out{'G', 'I', 'F', '8', '9', 'a'};
    putShort(out, width);
    putShort(out, height);
    out.push_back(static_cast<std::uint8_t>(0x80 | (7 << 4) | (tableBits - 1)));  // Global table, 8 bits per channel.
    out.push_back(0);                                                             // Background colour.
    out.push_back(0);                                                             // Square pixels.
    for (int i = 0; i < (1 << tableBits); ++i) {
        const Color color = i < static_cast<int>(palette.size()) ? palette[static_cast<std::size_t>(i)] : Color{};
        out.insert(out.end(), color.begin(), color.end());
    }
    // NETSCAPE2.0: loop forever.
    out.insert(out.end(), {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00});
    for (const Part& part : parts) {
        // Graphic control: keep the previous frame under this one (disposal 1), with its transparent index.
        out.insert(out.end(), {0x21, 0xF9, 0x04, static_cast<std::uint8_t>((1 << 2) | (part.transparency ? 1 : 0))});
        putShort(out, std::min(part.delay, 65535));
        out.push_back(transparent);
        out.push_back(0);
        out.push_back(0x2C);
        putShort(out, part.left);
        putShort(out, part.top);
        putShort(out, part.width);
        putShort(out, part.height);
        out.push_back(0);  // No local table, not interlaced.
        out.push_back(static_cast<std::uint8_t>(minimumCodeSize));
        const auto data = lzw(part.indices, minimumCodeSize);
        for (std::size_t i = 0; i < data.size(); i += 255) {
            const auto n = std::min<std::size_t>(255, data.size() - i);
            out.push_back(static_cast<std::uint8_t>(n));
            out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(i), data.begin() + static_cast<std::ptrdiff_t>(i + n));
        }
        out.push_back(0);
    }
    out.push_back(0x3B);
    return out;
}

}  // namespace siliconforge::gif
