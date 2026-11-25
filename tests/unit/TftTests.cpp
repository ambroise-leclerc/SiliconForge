// External TFT screens (#64, ADR 0010): a KMRTM28028 (ILI9341, SPI) and a MCUFRIEND 3.5" shield (ILI9481, 8-bit parallel bus).
// The bus is checked first against a known firmware (the commands, their parameters and the rate of the clock), then the
// pixels the firmware wrote are compared with an image that follows from the firmware's own definition of its picture.

#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/circuit/Tft.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/scenario/Scenario.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <webfront/testing/Bdd.hpp>

using namespace webfront::bdd;

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace siliconforge;
using namespace siliconforge::circuit;
using namespace std::chrono_literals;
using Json = nlohmann::json;

namespace {
const std::filesystem::path firmwareDir{SILICONFORGE_FIRMWARE_DIR};
const std::filesystem::path circuitDir{std::filesystem::path{SILICONFORGE_SOURCE_DIR} / "circuits"};

Json readCircuit(const char* name) {
    std::ifstream file{circuitDir / name};
    return Json::parse(file);
}

/// The picture both firmwares paint: a window in four quadrants, red and green above, blue and white below, on black.
std::vector<std::uint32_t> quadrants(unsigned width, unsigned height, unsigned left, unsigned top) {
    std::vector<std::uint32_t> image(static_cast<std::size_t>(width) * height, 0u);
    for (unsigned y = top; y < top + 80; ++y)
        for (unsigned x = left; x < left + 120; ++x) {
            const bool right = x >= left + 60, bottom = y >= top + 40;
            image[static_cast<std::size_t>(y) * width + x] = right ? (bottom ? 0xFFFFFFu : 0x00FF00u) : (bottom ? 0x0000FFu : 0xFF0000u);
        }
    return image;
}

/// The data bytes the firmware sends for that window: RGB565, high byte first.
std::uint64_t quadrantChecksum() {
    std::uint64_t sum = 1469598103934665603ull;
    const auto    feed = [&](std::uint8_t byte) { sum = (sum ^ byte) * 1099511628211ull; };
    for (unsigned y = 0; y < 80; ++y)
        for (unsigned x = 0; x < 120; ++x) {
            const bool right = x >= 60, bottom = y >= 40;
            const unsigned word = right ? (bottom ? 0xFFFFu : 0x07E0u) : (bottom ? 0x001Fu : 0xF800u);
            feed(static_cast<std::uint8_t>(word >> 8));
            feed(static_cast<std::uint8_t>(word));
        }
    return sum;
}

struct Run {
    testing::Bench bench;
    const Tft*     tft;
};

Run runFirmware(const char* hex, const char* circuitFile) {
    const auto path = firmwareDir / hex;
    if (!std::filesystem::exists(path))
        skip(path.string(), " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(readCircuit(circuitFile));
    bench.load(path);
    require(bench.runUntil([&] { return bench.pin("D12").level(); }, 1s, 4096));
    const auto* tft = bench.circuit().display("tft");
    require(tft != nullptr);
    return {std::move(bench), tft};
}

struct Command {
    std::uint8_t command;
    std::size_t  data;
};

/// What a controller received, command by command.
std::vector<Command> commandsOf(const Tft& tft) {
    std::vector<Command> result;
    for (const auto& transaction : tft.transactions())
        result.push_back({transaction.command, transaction.dataBytes});
    return result;
}

/// A bit-level driver of the SPI wires of a standalone controller, for the cases a firmware does not produce.
struct SpiWires {
    Tft&   tft;
    Cycles now{0};
    bool   cs{true}, reset{true};
    explicit SpiWires(Tft& target) : tft(target) { tft.power(true); }
    void level(bool dc, bool clock, bool data) { tft.spi(cs, dc, clock, data, reset, ++now); }
    void select(bool selected) { cs = !selected; level(false, false, false); }
    void bits(std::uint8_t value, bool dc, unsigned count = 8) {
        for (unsigned i = 0; i < count; ++i) {
            const bool bit = (value >> (7 - i)) & 1u;
            level(dc, false, bit);
            level(dc, true, bit);
            level(dc, false, bit);
        }
    }
    void command(std::uint8_t value) { select(true); bits(value, false); }
    void data(std::initializer_list<std::uint8_t> bytes) {
        for (const auto byte : bytes)
            bits(byte, true);
    }
    void data(std::size_t count, std::uint8_t value) {
        for (std::size_t i = 0; i < count; ++i)
            bits(value, true);
    }
};
}  // namespace

const Scenario aFirmwarePaintsAKmrtm28028Over{"a firmware paints a KMRTM28028 over SPI and the bus carries what it sent", {"circuit", "tft", "spi"}, [] {
    given("the tft_spi firmware on the Uno wired to the module, run until it raises D12", [&] {
        auto run = runFirmware("tft_spi.hex", "tft-spi-demo.json");
        const auto& tft = *run.tft;

        then("the commands are those of its initialisation, with their parameters, then the window and the pixels", [&] {
            const std::vector<Command> expected{{0x01, 0}, {0x11, 0}, {0x3A, 1}, {0x36, 1}, {0x29, 0}, {0x2A, 4}, {0x2B, 4}, {0x2C, 19200}};
            const auto                 actual = commandsOf(tft);
            require(actual.size() == expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                check(actual[i].command == expected[i].command);
                check(actual[i].data == expected[i].data);
            }
            const auto& log = tft.transactions();
            check(log[2].head == std::vector<std::uint8_t>{0x55});
            check(log[5].head == std::vector<std::uint8_t>{0, 60, 0, 179});
            check(log[6].head == std::vector<std::uint8_t>{0, 120, 0, 199});
            check(log[7].head[0] == 0xF8);  // The first pixel is red: 0xF800
            check(log[7].head[1] == 0x00);
            check(log[7].checksum == quadrantChecksum());
        });
        then("the clock ran at the rate the firmware chose: two cycles per bit, fck/2", [&] {
            for (const auto& transaction : tft.transactions())
                check(transaction.bitCycles == 2);
        });
        then("nothing was refused: no unknown command, no read, no byte cut short", [&] {
            check(tft.unknown() == 0);
            check(tft.unsupported() == 0);
            check(tft.framing() == 0);
            check(tft.pixelsWritten() == 9600);
            check(tft.dropped() == 0);
        });
        then("the panel is on, out of sleep and powered, and the backlight is lit", [&] {
            check(tft.displayOn());
            check(*run.bench.circuit().state("tft", "powered") == 1.0);
            check(*run.bench.circuit().state("tft", "backlight") == 1.0);
            check(*run.bench.circuit().state("tft", "commands") == 8.0);
        });

        then("the frame carries the counters and the revision, and the pixels are fetched apart as RGB565 in base64", [&] {
            const auto frame = run.bench.circuit().frame();
            check(!(frame["tft"].contains("screen")));
            check(frame["tft"]["pixelsWritten"] == 9600);
            const auto image = run.bench.circuit().screenImage("tft");
            check(image["width"] == 240);
            check(image["revision"] == frame["tft"]["revision"]);
            check(image["rgb565"].get<std::string>().size() == (240 * 320 * 2 + 2) / 3 * 4);
            check(run.bench.circuit().screenImage("xtal").is_null());
            check(run.bench.circuit().screenImage("no-such").is_null());
        });

        when("the pixels are compared with the image the firmware means to draw", [&] {
            const auto expected = quadrants(240, 320, 60, 120);
            const auto result   = compareImages(tft.shown(), expected, 240);
            then("they agree, pixel for pixel", [&] {
                check(result.differing == 0);
            });
        });
        when("they are compared with an image whose green and blue quadrants are exchanged", [&] {
            auto expected = quadrants(240, 320, 60, 120);
            for (auto& pixel : expected)
                pixel = pixel == 0x00FF00u ? 0x0000FFu : pixel == 0x0000FFu ? 0x00FF00u : pixel;
            const auto result = compareImages(tft.shown(), expected, 240);
            then("they diverge on exactly those two quadrants, and the first difference is where the green one starts", [&] {
                check(result.differing == 2 * 60 * 40);
                check(result.firstX == 120);
                check(result.firstY == 120);
            });
        });
        when("they are compared with the same image one pixel to the right", [&] {
            const auto result = compareImages(tft.shown(), quadrants(240, 320, 61, 120), 240);
            then("the edges differ: one column on each side of the window, and of the inner boundary", [&] {
                check(result.differing > 0);
                check(result.firstX == 60);
            });
        });
    });
}};

const Scenario aFirmwarePaintsAMcufriendShield{"a firmware paints a MCUFRIEND shield over its 8-bit parallel bus", {"circuit", "tft", "parallel"}, [] {
    given("the tft_parallel firmware wired as on the Uno shield, run until it raises D12", [&] {
        auto run = runFirmware("tft_parallel.hex", "tft-parallel-demo.json");
        const auto& tft = *run.tft;

        then("the controller is an ILI9481 of 320 x 480 pixels", [&] {
            check(tft.controller() == Tft::Controller::Ili9481);
            check(tft.width() == 320);
            check(tft.height() == 480);
        });
        then("the bytes taken at the rising edges of WR make the commands of the initialisation, the window and the pixels", [&] {
            const std::vector<Command> expected{{0x01, 0}, {0x11, 0}, {0xD0, 3}, {0xD1, 3}, {0xD2, 2}, {0xC0, 5}, {0xC5, 1},
                                                {0x3A, 1}, {0x36, 1}, {0x29, 0}, {0x2A, 4}, {0x2B, 4}, {0x2C, 19200}};
            const auto                 actual = commandsOf(tft);
            require(actual.size() == expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                check(actual[i].command == expected[i].command);
                check(actual[i].data == expected[i].data);
            }
            const auto& log = tft.transactions();
            check(log[2].head == std::vector<std::uint8_t>{0x07, 0x42, 0x18});
            check(log[10].head == std::vector<std::uint8_t>{0, 100, 0, 219});
            check(log[11].head == std::vector<std::uint8_t>{0, 200, 1, 23});
            check(log[12].checksum == quadrantChecksum());
            check(tft.unknown() == 0);
            check(tft.unsupported() == 0);
        });
        when("the pixels are compared with the image the firmware means to draw", [&] {
            then("they agree", [&] {
                check(compareImages(tft.shown(), quadrants(320, 480, 100, 200), 320).differing == 0);
            });
            then("they do not agree with the window painted at the same place on the other controller's 240-wide panel", [&] {
                check(compareImages(tft.shown(), quadrants(320, 480, 60, 200), 320).differing > 0);
            });
        });
    });
}};

const Scenario aModuleThatIsNotPowered{"a module that is not powered ignores its bus", {"circuit", "tft"}, [] {
    given("the SPI circuit without the wire from VCC to the positive rail", [&] {
        auto description = readCircuit("tft-spi-demo.json");
        Json wires = Json::array();
        for (const auto& wire : description["wires"])
            if (wire.at("to") != "T+21")  // VCC is at j21, wired to the positive rail.
                wires.push_back(wire);
        description["wires"] = wires;
        testing::Bench bench{mcu::create("atmega328p")};
        bench.attach(description);
        bench.load(firmwareDir / "tft_spi.hex");
        bench.runFor(300ms);
        then("the controller saw nothing and the screen is black", [&] {
            const auto* tft = bench.circuit().display("tft");
            require(tft != nullptr);
            check(!(tft->powered()));
            check(tft->commands() == 0);
            check(tft->pixelsWritten() == 0);
            check(*bench.circuit().state("tft", "powered") == 0.0);
        });
    });
}};

const Scenario theControllerFollowsItsCommands{"the controller follows its commands", {"tft"}, [] {
    given("an ILI9341 on a bus driven bit by bit", [&] {
        Tft      tft;
        SpiWires wires{tft};
        wires.reset = true;

        when("a command it does not have, and a command that reads it, are sent", [&] {
            wires.command(0xFF);
            wires.command(0x04);
            then("they are counted apart, and neither changes the screen", [&] {
                check(tft.unknown() == 1);
                check(tft.unsupported() == 1);
                check(tft.commands() == 2);
            });
        });
        when("CS is released after three bits of a byte", [&] {
            wires.select(true);
            wires.bits(0xFF, false, 3);
            wires.select(false);
            wires.select(true);
            wires.bits(0x11, false);
            then("the partial byte is lost and counted, and the next byte is whole", [&] {
                check(tft.framing() == 1);
                check(tft.commands() == 1);
                check(tft.transactions().back().command == 0x11);
            });
        });
        when("a pixel is written in 18 bits, its red on six bits", [&] {
            wires.command(0x2A);
            wires.data({0, 0, 0, 0});
            wires.command(0x2B);
            wires.data({0, 0, 0, 0});
            wires.command(0x36);
            wires.data({0x08});
            wires.command(0x3A);
            wires.data({0x66});
            wires.command(0x2C);
            wires.data({61 << 2, 0, 0});
            then("a picture that differs from it in the sixth bit of red only is a different picture", [&] {
                require(tft.fullDepth()[0]);
                auto expected = tft.memory();
                expected[0]   = 60u << 18;
                check(compareImages(tft.memory(), tft.memory(), 240, tft.fullDepth()).differing == 0);
                check(compareImages(tft.memory(), expected, 240, tft.fullDepth()).differing == 1);
                check(compareImages(tft.memory(), expected, 240).differing == 0);  // In 5-6-5 the two are the same colour
            });
        });
        when("more bytes follow a command than it has parameters, as after a command that is ignored", [&] {
            wires.command(0x11);
            wires.command(0x29);
            wires.command(0xFF);
            wires.data(100'000, 0xAA);  // The controller ignores them: none is kept
            wires.command(0x2A);
            wires.data({0, 10, 0, 11, 0, 99, 0, 99});  // Only the first four are the columns
            wires.command(0x2B);
            wires.data({0, 20, 0, 21});
            wires.command(0x3A);
            wires.data({0x55});
            wires.command(0x2C);
            wires.data({0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00});
            then("the window is the one the first four bytes gave", [&] {
                check(tft.shown()[20 * 240 + 11] != 0u);
                check(tft.shown()[21 * 240 + 10] != 0u);  // The third pixel wrapped to the next row: the window is 2 wide
                check(tft.shown()[20 * 240 + 12] == 0u);
            });
        });
        when("the window is 2 x 2 and four 16-bit pixels are written after sleep out and display on", [&] {
            wires.command(0x11);
            wires.command(0x29);
            wires.command(0x2A);
            wires.data({0, 10, 0, 11});
            wires.command(0x2B);
            wires.data({0, 20, 0, 21});
            wires.command(0x3A);
            wires.data({0x55});
            wires.command(0x36);
            wires.data({0x08});
            wires.command(0x2C);
            wires.data({0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F, 0xFF, 0xFF});
            const auto at = [&](unsigned x, unsigned y) { return tft.shown()[y * 240 + x]; };
            then("they land in reading order inside the window, in the colours of their words", [&] {
                check(at(10, 20) == 0xFF0000u);
                check(at(11, 20) == 0x00FF00u);
                check(at(10, 21) == 0x0000FFu);
                check(at(11, 21) == 0xFFFFFFu);
                check(at(12, 20) == 0u);
                check(tft.pixelsWritten() == 4);
            });
            then("inversion complements them and display off blanks them, the memory keeping its content", [&] {
                wires.command(0x21);
                check(at(10, 20) == 0x00FFFFu);
                wires.command(0x20);
                wires.command(0x28);
                check(at(10, 20) == 0u);
                check(tft.memory()[20 * 240 + 10] == 0xFF0000u);
            });
            then("a software reset puts the controller to sleep but keeps the pixels", [&] {
                wires.command(0x01);
                check(tft.sleeping());
                check(tft.memory()[20 * 240 + 10] == 0xFF0000u);
            });
        });
        when("the memory is mirrored, MX set, and one pixel is written at column 0", [&] {
            wires.command(0x11);
            wires.command(0x29);
            wires.command(0x3A);
            wires.data({0x55});
            wires.command(0x36);
            wires.data({0x48});
            wires.command(0x2A);
            wires.data({0, 0, 0, 0});
            wires.command(0x2B);
            wires.data({0, 0, 0, 0});
            wires.command(0x2C);
            wires.data({0xFF, 0xFF});
            then("it lands on the last column", [&] {
                check(tft.shown()[239] == 0xFFFFFFu);
                check(tft.shown()[0] == 0u);
            });
        });
        when("the rows and columns are exchanged, MV set, and the window runs along 3 columns of 320", [&] {
            wires.command(0x11);
            wires.command(0x29);
            wires.command(0x3A);
            wires.data({0x55});
            wires.command(0x36);
            wires.data({0x28});
            wires.command(0x2A);
            wires.data({0, 0, 0, 2});
            wires.command(0x2B);
            wires.data({0, 0, 0, 0});
            wires.command(0x2C);
            wires.data({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
            then("they go down the first column of the panel", [&] {
                check(tft.shown()[0] == 0xFFFFFFu);
                check(tft.shown()[240] == 0xFFFFFFu);
                check(tft.shown()[480] == 0xFFFFFFu);
                check(tft.shown()[1] == 0u);
            });
        });
        when("18 bits per pixel are chosen and a pixel is written", [&] {
            wires.command(0x11);
            wires.command(0x29);
            wires.command(0x3A);
            wires.data({0x66});
            wires.command(0x36);
            wires.data({0x08});
            wires.command(0x2C);
            wires.data({0xFC, 0x00, 0x80});
            then("each channel keeps its six bits", [&] {
                check(tft.shown()[0] == 0xFF0082u);
            });
        });
        when("RESET is pulled low in the middle of a session", [&] {
            wires.command(0x11);
            wires.command(0x29);
            check(tft.displayOn());
            wires.reset = false;
            wires.level(false, false, false);
            then("the controller is asleep again, display off", [&] {
                check(!(tft.displayOn()));
                check(tft.sleeping());
            });
        });
    });
}};

namespace {
/// A scenario of the SPI demo that compares the screen with `image`, in a directory of its own.
struct ScreenScenario {
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "siliconforge-tft-scenario";

    ScreenScenario() { std::filesystem::create_directories(directory); }

    void write(const std::string& name, const std::vector<std::uint32_t>& pixels, unsigned width, unsigned height, const std::string& lineEnd = "\n") const {
        std::ofstream file{directory / name, std::ios::binary};
        file << "P6" << lineEnd << width << ' ' << height << lineEnd << "255" << lineEnd;
        for (const auto pixel : pixels) {
            const char rgb[3] = {static_cast<char>(pixel >> 16), static_cast<char>(pixel >> 8), static_cast<char>(pixel)};
            file.write(rgb, 3);
        }
    }

    scenario::Report run(const std::string& screen, const std::string& component = "tft", const std::filesystem::path& circuit = circuitDir / "tft-spi-demo.json") const {
        const Json document = {
            {"formatVersion", 1},
            {"name", "tft"},
            {"circuit", circuit.string()},
            {"firmware", (firmwareDir / "tft_spi.hex").string()},
            {"steps", {{{"waitFor", {{"pin", "D12"}, {"state", "high"}}}, {"timeout", "500ms"}},
                       {{"screen", {{"component", component}, {"image", screen}}}}}}};
        auto prepared = scenario::Scenario::prepare(document.dump(), directory, "tft.json");
        if (!prepared) {
            scenario::Report report;
            report.verdict = scenario::Verdict::Incompatible;
            report.problem = prepared.error();
            return report;
        }
        return (*prepared)->run();
    }
};
}  // namespace

const Scenario aScenarioComparesTheScreenWith{"a scenario compares the screen with an expected picture", {"scenario", "tft"}, [] {
    if (!std::filesystem::exists(firmwareDir / "tft_spi.hex"))
        skip("tft_spi.hex", " is missing: build the firmware (see firmware/README.md)");
    given("the SPI demo and a picture that matches what the firmware draws", [&] {
        ScreenScenario fixture;
        fixture.write("expected.ppm", quadrants(240, 320, 60, 120), 240, 320);

        when("the scenario runs", [&] {
            const auto report = fixture.run("expected.ppm");
            then("it passes, and the report says how many pixels differ: none", [&] {
                require(report.verdict == scenario::Verdict::Pass);
                require(report.screens.size() == 1);
                check(report.screens[0].differing == 0);
                check(report.screens[0].width == 240);
                check(report.steps[1].kind == "screen");
                // Read at the end of the instruction under way when the step is due: at most a few cycles later.
                check(report.screens[0].observedAt >= report.screens[0].at);
                check(report.screens[0].observedAt - report.screens[0].at < 8);
            });
            then("the picture is one of the inputs, identified by its hash", [&] {
                const auto image = std::ranges::find(report.inputs, std::string{"image"}, &scenario::Input::role);
                require(image != report.inputs.end());
                check(image->path == "expected.ppm");
                check(image->sha256.size() == 64);
            });
        });
    });
    given("the picture the firmware draws, but a module whose LED pin is not wired to the supply", [&] {
        ScreenScenario fixture;
        fixture.write("expected.ppm", quadrants(240, 320, 60, 120), 240, 320);
        auto description = Json::parse(std::ifstream{circuitDir / "tft-spi-demo.json"});
        auto wires       = Json::array();
        for (const auto& wire : description.at("wires"))
            if (wire.at("from") != "h28")  // The wire from the LED pin of the module to the +5 V rail
                wires.push_back(wire);
        require(wires.size() + 1 == description.at("wires").size());
        description["wires"] = wires;
        const auto dark      = fixture.directory / "dark.json";
        std::ofstream{dark} << description.dump();

        when("the scenario runs", [&] {
            const auto report = fixture.run("expected.ppm", "tft", dark);
            then("the screen is black whatever the memory holds, so the picture does not match", [&] {
                require(report.verdict == scenario::Verdict::Fail);
                require(report.screens.size() == 1);
                check(report.screens[0].differing == 9600);  // The painted window, black instead of its colours
            });
        });
    });
    given("the same picture saved with Windows line ends (CRLF) in its header", [&] {
        ScreenScenario fixture;
        fixture.write("crlf.ppm", quadrants(240, 320, 60, 120), 240, 320, "\r\n");
        // And one whose first pixel starts with the bytes 0D 0A, after a header that ends with a single LF: those are pixel data
        auto pixels = quadrants(240, 320, 60, 120);
        pixels[0]   = 0x0D0A0Bu;
        fixture.write("lf.ppm", pixels, 240, 320);

        then("the CRLF pair ends the header: the pixels are not shifted by a byte and the screen matches", [&] {
            const auto report = fixture.run("crlf.ppm");
            require(report.verdict == scenario::Verdict::Pass);
            check(report.screens[0].differing == 0);
        });
        then("a single LF still ends only itself: the bytes after it are the first pixel", [&] {
            const auto report = fixture.run("lf.ppm");
            require(report.screens.size() == 1);
            check(report.screens[0].differing == 1);  // Only the first pixel differs: the picture was not shifted
            check(report.screens[0].firstX == 0);
            check(report.screens[0].firstY == 0);
        });
    });
    given("a picture whose green and blue quadrants are exchanged", [&] {
        ScreenScenario fixture;
        auto           expected = quadrants(240, 320, 60, 120);
        for (auto& pixel : expected)
            pixel = pixel == 0x00FF00u ? 0x0000FFu : pixel == 0x0000FFu ? 0x00FF00u : pixel;
        fixture.write("diverging.ppm", expected, 240, 320);

        when("the scenario runs", [&] {
            const auto report = fixture.run("diverging.ppm");
            then("it fails, and says where the first difference is", [&] {
                require(report.verdict == scenario::Verdict::Fail);
                require(report.screens.size() == 1);
                check(report.screens[0].differing == 4800);
                check(report.screens[0].firstX == 120);
                check(report.screens[0].firstY == 120);
                check(!(report.screens[0].passed));
                check(report.steps[1].outcome == scenario::StepRecord::Outcome::Failed);
                check(report.steps[1].detail.find("(120, 120)") != std::string::npos);
                check(scenario::exitCode(report.verdict) == 1);
            });
        });
    });
    given("pictures the screen cannot match", [&] {
        ScreenScenario fixture;
        fixture.write("small.ppm", std::vector<std::uint32_t>(4, 0u), 2, 2);

        then("one of the wrong size is refused before the run, as beyond what the screen shows", [&] {
            const auto report = fixture.run("small.ppm");
            require(report.verdict == scenario::Verdict::Incompatible);
            require(report.problem);
            check(report.problem->cause == scenario::Problem::Cause::Capacity);
            check(report.problem->path == "/steps/1/screen/image");
        });
        then("a missing file is refused as unreadable", [&] {
            const auto report = fixture.run("absent.ppm");
            require(report.problem);
            check(report.problem->cause == scenario::Problem::Cause::Unreadable);
        });
        then("a component that is not a display is refused as a missing resource", [&] {
            const auto report = fixture.run("small.ppm", "xtal");
            require(report.problem);
            check(report.problem->cause == scenario::Problem::Cause::Resource);
        });
    });
}};
