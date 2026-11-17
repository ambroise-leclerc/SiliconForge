#pragma once
// The pieces of tools/captures2gif: scaling, one palette for a whole sequence, and an animated GIF encoder.

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace siliconforge::gif {

using Color = std::array<std::uint8_t, 3>;

/// An RGB image, 3 bytes per pixel, row after row.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
};

/// Scales with a Lanczos-3 filter, widened when shrinking so that every source pixel contributes.
Image resize(const Image& source, int width, int height);

/// At most `colors` colours (1-255) chosen by median cut over the pixels of `images`.
std::vector<Color> medianCut(std::span<const Image> images, int colors);

/// The index of the nearest palette colour of every pixel, without dithering.
std::vector<std::uint8_t> quantize(const Image& image, std::span<const Color> palette);

/// An animated GIF that loops forever. Every frame has the size given and is shown for `delayCentiseconds`; only the
/// rectangle that changed is stored, with its unchanged pixels transparent, and repeated frames lengthen the previous
/// one. `palette` holds at most 255 colours: one index is kept for transparency.
std::vector<std::uint8_t> encode(int width, int height, std::span<const Color> palette,
                                 std::span<const std::vector<std::uint8_t>> frames, int delayCentiseconds);

/// GIF's variable-width LZW of `indices`, with `minimumCodeSize` bits per index (2-8), before its sub-blocks.
std::vector<std::uint8_t> lzw(std::span<const std::uint8_t> indices, int minimumCodeSize);

}  // namespace siliconforge::gif
