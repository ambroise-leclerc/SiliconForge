// captures2gif: assembles the PNG sequence recorded by a UI spec (webFrontCapture.record) into an animated GIF.
//
//     build/tools/captures2gif build/tests/webfront-jasmine/UI-Tour/captures/demo docs/images/siliconforge.gif
//
// The frames are scaled to --width and share one palette, which keeps the file small.
#include "Gif.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using siliconforge::gif::Image;

constexpr std::string_view usage =
    "usage: captures2gif [-h] [--width WIDTH] [--fps FPS] [--colors COLORS] frames output";

[[noreturn]] void usageError(const std::string& message) {
    std::println(stderr, "{}\ncaptures2gif: error: {}", usage, message);
    std::exit(2);
}

template <typename T>
T parseNumber(std::string_view name, std::string_view text) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        usageError("argument " + std::string(name) + ": invalid value: '" + std::string(text) + "'");
    return value;
}

Image load(const fs::path& file) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(file.string().c_str(), &width, &height, &channels, 3);
    if (!pixels)
        throw std::runtime_error(file.string() + ": " + stbi_failure_reason());
    Image image{width, height, std::vector<std::uint8_t>(pixels, pixels + static_cast<std::size_t>(width) * height * 3)};
    stbi_image_free(pixels);
    return image;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> positional;
    int width = 960;
    double fps = 10.0;
    int colors = 160;
    for (int i = 1; i < argc; ++i) {
        std::string_view argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            std::println("{}\n\nAssembles the PNG sequence recorded by a UI spec (webFrontCapture.record) into an animated "
                         "GIF.\n\npositional arguments:\n  frames           directory of 0001.png, 0002.png...\n  output\n\n"
                         "options:\n  --width WIDTH    (960)\n  --fps FPS        rate the sequence was recorded at (10)\n"
                         "  --colors COLORS  (160, at most 255)",
                         usage);
            return 0;
        }
        std::string_view value;
        std::string_view name = argument;
        if (const auto equal = argument.find('='); argument.starts_with("--") && equal != std::string_view::npos) {
            name = argument.substr(0, equal);
            value = argument.substr(equal + 1);
        } else if (argument == "--width" || argument == "--fps" || argument == "--colors") {
            if (i + 1 >= argc)
                usageError("argument " + std::string(argument) + ": expected one argument");
            value = argv[++i];
        } else if (argument.starts_with("--")) {
            usageError("unrecognized arguments: " + std::string(argument));
        } else {
            positional.emplace_back(argument);
            continue;
        }
        if (name == "--width")
            width = parseNumber<int>(name, value);
        else if (name == "--fps")
            fps = parseNumber<double>(name, value);
        else if (name == "--colors")
            colors = parseNumber<int>(name, value);
        else
            usageError("unrecognized arguments: " + std::string(argument));
    }
    if (positional.size() != 2)
        usageError(positional.size() < 2 ? "the following arguments are required: frames, output"
                                         : "unrecognized arguments: " + positional[2]);
    if (width < 1 || width > 65535 || !(fps > 0.0) || colors < 1 || colors > 255)
        usageError("--width between 1 and 65535, --fps above 0, --colors between 1 and 255");
    const fs::path directory = positional[0];
    const fs::path output = positional[1];

    try {
        std::vector<fs::path> files;
        if (fs::is_directory(directory))
            for (const auto& entry : fs::directory_iterator(directory))
                if (entry.is_regular_file() && entry.path().extension() == ".png")
                    files.push_back(entry.path());
        std::ranges::sort(files);
        if (files.empty()) {
            std::println(stderr, "{}: no PNG frame", directory.string());
            return 1;
        }
        std::vector<Image> frames;
        for (const auto& file : files) {
            const Image image = load(file);
            const int height = static_cast<int>(std::lround(static_cast<double>(image.height) * width / image.width));
            frames.push_back(siliconforge::gif::resize(image, width, std::max(1, height)));
            if (frames.back().height != frames.front().height)
                throw std::runtime_error(file.string() + ": the frames do not share one size");
        }
        // One palette for the whole animation, so that unchanged areas compress to nothing. It is computed over frames
        // spread along the sequence: a colour that only appears later (an LED that lights up) is in it.
        std::vector<Image> sample;
        for (std::size_t i = 0; i < frames.size(); i += std::max<std::size_t>(1, frames.size() / 8))
            sample.push_back(frames[i]);
        const auto palette = siliconforge::gif::medianCut(sample, colors);
        std::vector<std::vector<std::uint8_t>> indexed;
        for (const Image& frame : frames)
            indexed.push_back(siliconforge::gif::quantize(frame, palette));
        const int delay = std::max(1, static_cast<int>(std::lround(100.0 / fps)));
        const auto bytes = siliconforge::gif::encode(width, frames.front().height, palette, indexed, delay);
        std::ofstream file(output, std::ios::binary);
        if (!file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("cannot write " + output.string());
        std::println("{}: {} frames, {:.1f} s, {:.0f} KiB", output.string(), frames.size(), static_cast<double>(frames.size()) / fps,
                     static_cast<double>(bytes.size()) / 1024.0);
    } catch (const std::exception& error) {
        std::println(stderr, "captures2gif: {}", error.what());
        return 1;
    }
    return 0;
}
