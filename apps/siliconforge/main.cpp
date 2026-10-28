// siliconforge: runs a firmware on an emulated MCU, with a web interface (WebFront) or headless.

#include "Simulation.hpp"
#include "WebBridge.hpp"

#include <siliconforge/mcu/Registry.hpp>

#include <webfront/WebFront.hpp>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <string_view>

using namespace siliconforge;

namespace {

#if defined(WEBFRONT_EMBED_WEBKITGTK) || defined(WEBFRONT_EMBED_WEBVIEW2) || defined(WEBFRONT_EMBED_WKWEBVIEW)
constexpr bool nativeWindowBuilt = true;
using NativeWebFront             = webfront::WebFrontWithFrontend<webfront::frontend::NativeWebViewFrontend>;
#else
constexpr bool nativeWindowBuilt = false;
#endif

/// Where the interface is displayed.
enum class Display { Window, Browser, None };

struct Options {
    std::string                  mcu{"atmega328p"};
    std::optional<std::filesystem::path> firmware;
    bool                         headless{false};
    Display                      display{nativeWindowBuilt ? Display::Window : Display::Browser};
    double                       duration{0};  ///< Simulated seconds for headless runs, 0 = until stopped.
    double                       speed{1.0};   ///< 0 = as fast as possible.
    std::string                  address{"127.0.0.1"};
    std::string                  port{"9002"};
    std::filesystem::path        ui{SILICONFORGE_UI_DIR};
};

void printUsage() {
    std::println(R"(Usage: siliconforge [options] [firmware.hex|firmware.elf]

Runs a firmware on an emulated microcontroller and shows its interface in a window{}. The interface is also served
on http://<address>:<port>/.

Options:
  --mcu <name>        Microcontroller to emulate (default atmega328p)
  --headless          No interface: run, print the serial output on stdout and report the emulation speed
  --duration <s>      Simulated seconds to run in headless mode (default: until interrupted)
  --speed <factor>    1 = real time (default), 0 or "max" = as fast as possible
  --address <ip>      Address to listen on (default 127.0.0.1; 0.0.0.0 to serve other machines)
  --port <port>       HTTP port (default 9002, 0 for any free port)
  --browser           Show the interface in the system browser instead of a window
  --no-browser        Serve the interface without opening a window or a browser
  --ui <directory>    Interface files (default: the ui/ directory of the sources)
  --help              This help)",
                 nativeWindowBuilt ? "" : " (this build has no native window engine: the system browser)");
    std::print("Available microcontrollers:");
    for (auto name : mcu::available())
        std::print(" {}", name);
    std::println();
}

std::optional<double> parseNumber(std::string_view text) {
    if (text == "max")
        return 0.0;
    double value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? std::optional{value} : std::nullopt;
}

std::optional<Options> parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        const auto             value = [&]() -> std::optional<std::string_view> {
            if (i + 1 >= argc)
                return std::nullopt;
            return std::string_view{argv[++i]};
        };
        if (argument == "--help" || argument == "-h")
            return std::nullopt;
        if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--browser") {
            options.display = Display::Browser;
        } else if (argument == "--no-browser") {
            options.display = Display::None;
        } else if (argument == "--mcu" || argument == "--address" || argument == "--port" || argument == "--ui") {
            const auto text = value();
            if (!text)
                return std::nullopt;
            if (argument == "--mcu")
                options.mcu = *text;
            else if (argument == "--address")
                options.address = *text;
            else if (argument == "--port")
                options.port = *text;
            else
                options.ui = *text;
        } else if (argument == "--duration" || argument == "--speed") {
            const auto text   = value();
            const auto number = text ? parseNumber(*text) : std::nullopt;
            if (!number || *number < 0)
                return std::nullopt;
            (argument == "--duration" ? options.duration : options.speed) = *number;
        } else if (!argument.starts_with("--") && !options.firmware) {
            options.firmware = std::filesystem::path{argument};
        } else {
            std::println(stderr, "Unknown argument: {}", argument);
            return std::nullopt;
        }
    }
    return options;
}

int runHeadless(Mcu& mcu, const Options& options) {
    if (auto* port = mcu.serial(0))
        port->onTransmit([](u8 byte, Cycles) {
            std::putchar(byte);
            std::fflush(stdout);
        });
    const auto start       = std::chrono::steady_clock::now();
    const auto sliceCycles = mcu.clockHz() / 100;
    const auto endCycles   = options.duration > 0 ? mcu.cyclesFor(std::chrono::duration<double>(options.duration)) : Cycles{0};
    auto       reason      = StopReason::BudgetReached;
    while (options.duration <= 0 || mcu.cycles() < endCycles) {
        const auto budget = options.duration > 0 ? std::min(sliceCycles, endCycles - mcu.cycles()) : sliceCycles;
        reason            = mcu.run(budget).reason;
        if (reason != StopReason::BudgetReached)
            break;
        if (options.speed > 0) {
            const auto simulated = std::chrono::duration<double>(static_cast<double>(mcu.cycles()) / static_cast<double>(mcu.clockHz()) / options.speed);
            std::this_thread::sleep_until(start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(simulated));
        }
    }
    const auto wall      = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto simulated = static_cast<double>(mcu.cycles()) / static_cast<double>(mcu.clockHz());
    std::println(stderr, "\n[siliconforge] {} simulated s in {:.3f} s ({:.1f} MHz effective, x{:.1f} real time), stop: {}", simulated, wall,
                 static_cast<double>(mcu.cycles()) / wall / 1e6, simulated / wall, app::toString(reason));
    if (reason != StopReason::BudgetReached)
        std::println(stderr, "[siliconforge] PC = {:#06x} {}", mcu.programCounter(), mcu.symbolAt(mcu.programCounter()));
    return reason == StopReason::BudgetReached ? 0 : 2;
}

webfront::WebFrontConfig webConfig(const Options& options) {
    return {.address = options.address, .port = options.port, .documentRoot = options.ui};
}

/// Serves the interface until the window is closed or, without a window, until the program is stopped.
template <typename App>
void serve(App& web, app::Simulation& simulation, const Options& options, const std::string& firmwareName) {
    app::WebBridge<App> bridge{web, simulation, firmwareName};
    if (options.firmware)
        simulation.run();
    std::println("SiliconForge: http://{}:{}/", options.address == "0.0.0.0" ? "localhost" : options.address, web.port());
    if (options.display == Display::None)
        web.run();
    else
        web.show("index.html");
}

}  // namespace

int main(int argc, char** argv) {
    const auto options = parseOptions(argc, argv);
    if (!options) {
        printUsage();
        return 1;
    }
    auto device = mcu::create(options->mcu);
    if (!device) {
        std::println(stderr, "Unknown microcontroller '{}'", options->mcu);
        printUsage();
        return 1;
    }
    std::string firmwareName;
    if (options->firmware) {
        auto image = loadProgram(*options->firmware);
        if (!image) {
            std::println(stderr, "{}: {}{}", options->firmware->string(), image.error().message,
                         image.error().line ? std::format(" (line {})", image.error().line) : std::string{});
            return 1;
        }
        device->load(*image);
        firmwareName = options->firmware->filename().string();
    } else if (options->headless) {
        std::println(stderr, "--headless needs a firmware");
        return 1;
    }

    if (options->headless)
        return runHeadless(*device, *options);

    try {
        app::Simulation simulation{std::move(device)};
        simulation.setSpeed(options->speed);
#if defined(WEBFRONT_EMBED_WEBKITGTK) || defined(WEBFRONT_EMBED_WEBVIEW2) || defined(WEBFRONT_EMBED_WKWEBVIEW)
        if (options->display == Display::Window) {
            std::optional<NativeWebFront> window;
            try {
                window.emplace(webConfig(*options));
            } catch (const std::exception& error) {
                // Typically no graphical session (no DISPLAY or WAYLAND_DISPLAY): fall back to the browser.
                std::println(stderr, "siliconforge: no native window ({}), using the system browser", error.what());
            }
            if (window) {
                serve(*window, simulation, *options, firmwareName);
                return 0;
            }
        }
#endif
        webfront::WebFront web{webConfig(*options)};
        serve(web, simulation, *options, firmwareName);
    } catch (const std::exception& error) {
        std::println(stderr, "siliconforge: {}", error.what());
        return 1;
    }
}
