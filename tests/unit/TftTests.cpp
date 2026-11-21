// External TFT screens (#64, ADR 0010): a KMRTM28028 (ILI9341, SPI) and a MCUFRIEND 3.5" shield (ILI9481, 8-bit parallel bus).
// The bus is checked first against a known firmware (the commands, their parameters and the rate of the clock), then the
// pixels the firmware wrote are compared with an image that follows from the firmware's own definition of its picture.

#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/circuit/Tft.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/scenario/Scenario.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <catch2/catch_test_macros.hpp>

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
        SKIP(path.string() << " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(readCircuit(circuitFile));
    bench.load(path);
    REQUIRE(bench.runUntil([&] { return bench.pin("D12").level(); }, 1s, 4096));
    const auto* tft = bench.circuit().display("tft");
    REQUIRE(tft != nullptr);
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
};
}  // namespace

SCENARIO("a firmware paints a KMRTM28028 over SPI and the bus carries what it sent", "[circuit][tft][spi]") {
    GIVEN("the tft_spi firmware on the Uno wired to the module, run until it raises D12") {
        auto run = runFirmware("tft_spi.hex", "tft-spi-demo.json");
        const auto& tft = *run.tft;

        THEN("the commands are those of its initialisation, with their parameters, then the window and the pixels") {
            const std::vector<Command> expected{{0x01, 0}, {0x11, 0}, {0x3A, 1}, {0x36, 1}, {0x29, 0}, {0x2A, 4}, {0x2B, 4}, {0x2C, 19200}};
            const auto                 actual = commandsOf(tft);
            REQUIRE(actual.size() == expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                CHECK(actual[i].command == expected[i].command);
                CHECK(actual[i].data == expected[i].data);
            }
            const auto& log = tft.transactions();
            CHECK(log[2].head == std::vector<std::uint8_t>{0x55});
            CHECK(log[5].head == std::vector<std::uint8_t>{0, 60, 0, 179});
            CHECK(log[6].head == std::vector<std::uint8_t>{0, 120, 0, 199});
            CHECK(log[7].head[0] == 0xF8);  // The first pixel is red: 0xF800
            CHECK(log[7].head[1] == 0x00);
            CHECK(log[7].checksum == quadrantChecksum());
        }
        THEN("the clock ran at the rate the firmware chose: two cycles per bit, fck/2") {
            for (const auto& transaction : tft.transactions())
                CHECK(transaction.bitCycles == 2);
        }
        THEN("nothing was refused: no unknown command, no read, no byte cut short") {
            CHECK(tft.unknown() == 0);
            CHECK(tft.unsupported() == 0);
            CHECK(tft.framing() == 0);
            CHECK(tft.pixelsWritten() == 9600);
            CHECK(tft.dropped() == 0);
        }
        THEN("the panel is on, out of sleep and powered, and the backlight is lit") {
            CHECK(tft.displayOn());
            CHECK(*run.bench.circuit().state("tft", "powered") == 1.0);
            CHECK(*run.bench.circuit().state("tft", "backlight") == 1.0);
            CHECK(*run.bench.circuit().state("tft", "commands") == 8.0);
        }

        THEN("the frame of the circuit carries the pixels once, as RGB565 in base64, then only the counters") {
            auto first = run.bench.circuit().frame();
            REQUIRE(first["tft"].contains("screen"));
            CHECK(first["tft"]["screen"]["width"] == 240);
            CHECK(first["tft"]["screen"]["rgb565"].get<std::string>().size() == (240 * 320 * 2 + 2) / 3 * 4);
            CHECK(first["tft"]["pixelsWritten"] == 9600);
            CHECK_FALSE(run.bench.circuit().frame()["tft"].contains("screen"));
        }

        WHEN("the pixels are compared with the image the firmware means to draw") {
            const auto expected = quadrants(240, 320, 60, 120);
            const auto result   = compareImages(tft.shown(), expected, 240);
            THEN("they agree, pixel for pixel") {
                CHECK(result.differing == 0);
            }
        }
        WHEN("they are compared with an image whose green and blue quadrants are exchanged") {
            auto expected = quadrants(240, 320, 60, 120);
            for (auto& pixel : expected)
                pixel = pixel == 0x00FF00u ? 0x0000FFu : pixel == 0x0000FFu ? 0x00FF00u : pixel;
            const auto result = compareImages(tft.shown(), expected, 240);
            THEN("they diverge on exactly those two quadrants, and the first difference is where the green one starts") {
                CHECK(result.differing == 2 * 60 * 40);
                CHECK(result.firstX == 120);
                CHECK(result.firstY == 120);
            }
        }
        WHEN("they are compared with the same image one pixel to the right") {
            const auto result = compareImages(tft.shown(), quadrants(240, 320, 61, 120), 240);
            THEN("the edges differ: one column on each side of the window, and of the inner boundary") {
                CHECK(result.differing > 0);
                CHECK(result.firstX == 60);
            }
        }
    }
}

SCENARIO("a firmware paints a MCUFRIEND shield over its 8-bit parallel bus", "[circuit][tft][parallel]") {
    GIVEN("the tft_parallel firmware wired as on the Uno shield, run until it raises D12") {
        auto run = runFirmware("tft_parallel.hex", "tft-parallel-demo.json");
        const auto& tft = *run.tft;

        THEN("the controller is an ILI9481 of 320 x 480 pixels") {
            CHECK(tft.controller() == Tft::Controller::Ili9481);
            CHECK(tft.width() == 320);
            CHECK(tft.height() == 480);
        }
        THEN("the bytes taken at the rising edges of WR make the commands of the initialisation, the window and the pixels") {
            const std::vector<Command> expected{{0x01, 0}, {0x11, 0}, {0xD0, 3}, {0xD1, 3}, {0xD2, 2}, {0xC0, 5}, {0xC5, 1},
                                                {0x3A, 1}, {0x36, 1}, {0x29, 0}, {0x2A, 4}, {0x2B, 4}, {0x2C, 19200}};
            const auto                 actual = commandsOf(tft);
            REQUIRE(actual.size() == expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                CHECK(actual[i].command == expected[i].command);
                CHECK(actual[i].data == expected[i].data);
            }
            const auto& log = tft.transactions();
            CHECK(log[2].head == std::vector<std::uint8_t>{0x07, 0x42, 0x18});
            CHECK(log[10].head == std::vector<std::uint8_t>{0, 100, 0, 219});
            CHECK(log[11].head == std::vector<std::uint8_t>{0, 200, 1, 23});
            CHECK(log[12].checksum == quadrantChecksum());
            CHECK(tft.unknown() == 0);
            CHECK(tft.unsupported() == 0);
        }
        WHEN("the pixels are compared with the image the firmware means to draw") {
            THEN("they agree") {
                CHECK(compareImages(tft.shown(), quadrants(320, 480, 100, 200), 320).differing == 0);
            }
            THEN("they do not agree with the window painted at the same place on the other controller's 240-wide panel") {
                CHECK(compareImages(tft.shown(), quadrants(320, 480, 60, 200), 320).differing > 0);
            }
        }
    }
}

SCENARIO("a module that is not powered ignores its bus", "[circuit][tft]") {
    GIVEN("the SPI circuit without the wire from VCC to the positive rail") {
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
        THEN("the controller saw nothing and the screen is black") {
            const auto* tft = bench.circuit().display("tft");
            REQUIRE(tft != nullptr);
            CHECK_FALSE(tft->powered());
            CHECK(tft->commands() == 0);
            CHECK(tft->pixelsWritten() == 0);
            CHECK(*bench.circuit().state("tft", "powered") == 0.0);
        }
    }
}

SCENARIO("the controller follows its commands", "[tft]") {
    GIVEN("an ILI9341 on a bus driven bit by bit") {
        Tft      tft;
        SpiWires wires{tft};
        wires.reset = true;

        WHEN("a command it does not have, and a command that reads it, are sent") {
            wires.command(0xFF);
            wires.command(0x04);
            THEN("they are counted apart, and neither changes the screen") {
                CHECK(tft.unknown() == 1);
                CHECK(tft.unsupported() == 1);
                CHECK(tft.commands() == 2);
            }
        }
        WHEN("CS is released after three bits of a byte") {
            wires.select(true);
            wires.bits(0xFF, false, 3);
            wires.select(false);
            wires.select(true);
            wires.bits(0x11, false);
            THEN("the partial byte is lost and counted, and the next byte is whole") {
                CHECK(tft.framing() == 1);
                CHECK(tft.commands() == 1);
                CHECK(tft.transactions().back().command == 0x11);
            }
        }
        WHEN("the window is 2 x 2 and four 16-bit pixels are written after sleep out and display on") {
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
            THEN("they land in reading order inside the window, in the colours of their words") {
                CHECK(at(10, 20) == 0xFF0000u);
                CHECK(at(11, 20) == 0x00FF00u);
                CHECK(at(10, 21) == 0x0000FFu);
                CHECK(at(11, 21) == 0xFFFFFFu);
                CHECK(at(12, 20) == 0u);
                CHECK(tft.pixelsWritten() == 4);
            }
            THEN("inversion complements them and display off blanks them, the memory keeping its content") {
                wires.command(0x21);
                CHECK(at(10, 20) == 0x00FFFFu);
                wires.command(0x20);
                wires.command(0x28);
                CHECK(at(10, 20) == 0u);
                CHECK(tft.memory()[20 * 240 + 10] == 0xFF0000u);
            }
            THEN("a software reset puts the controller to sleep but keeps the pixels") {
                wires.command(0x01);
                CHECK(tft.sleeping());
                CHECK(tft.memory()[20 * 240 + 10] == 0xFF0000u);
            }
        }
        WHEN("the memory is mirrored, MX set, and one pixel is written at column 0") {
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
            THEN("it lands on the last column") {
                CHECK(tft.shown()[239] == 0xFFFFFFu);
                CHECK(tft.shown()[0] == 0u);
            }
        }
        WHEN("the rows and columns are exchanged, MV set, and the window runs along 3 columns of 320") {
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
            THEN("they go down the first column of the panel") {
                CHECK(tft.shown()[0] == 0xFFFFFFu);
                CHECK(tft.shown()[240] == 0xFFFFFFu);
                CHECK(tft.shown()[480] == 0xFFFFFFu);
                CHECK(tft.shown()[1] == 0u);
            }
        }
        WHEN("18 bits per pixel are chosen and a pixel is written") {
            wires.command(0x11);
            wires.command(0x29);
            wires.command(0x3A);
            wires.data({0x66});
            wires.command(0x36);
            wires.data({0x08});
            wires.command(0x2C);
            wires.data({0xFC, 0x00, 0x80});
            THEN("each channel keeps its six bits") {
                CHECK(tft.shown()[0] == 0xFF0082u);
            }
        }
        WHEN("RESET is pulled low in the middle of a session") {
            wires.command(0x11);
            wires.command(0x29);
            CHECK(tft.displayOn());
            wires.reset = false;
            wires.level(false, false, false);
            THEN("the controller is asleep again, display off") {
                CHECK_FALSE(tft.displayOn());
                CHECK(tft.sleeping());
            }
        }
    }
}

namespace {
/// A scenario of the SPI demo that compares the screen with `image`, in a directory of its own.
struct ScreenScenario {
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "siliconforge-tft-scenario";

    ScreenScenario() { std::filesystem::create_directories(directory); }

    void write(const std::string& name, const std::vector<std::uint32_t>& pixels, unsigned width, unsigned height) const {
        std::ofstream file{directory / name, std::ios::binary};
        file << "P6\n" << width << ' ' << height << "\n255\n";
        for (const auto pixel : pixels) {
            const char rgb[3] = {static_cast<char>(pixel >> 16), static_cast<char>(pixel >> 8), static_cast<char>(pixel)};
            file.write(rgb, 3);
        }
    }

    scenario::Report run(const std::string& screen, const std::string& component = "tft") const {
        const Json document = {
            {"formatVersion", 1},
            {"name", "tft"},
            {"circuit", (circuitDir / "tft-spi-demo.json").string()},
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

SCENARIO("a scenario compares the screen with an expected picture", "[scenario][tft]") {
    if (!std::filesystem::exists(firmwareDir / "tft_spi.hex"))
        SKIP("tft_spi.hex is missing: build the firmware (see firmware/README.md)");
    GIVEN("the SPI demo and a picture that matches what the firmware draws") {
        ScreenScenario fixture;
        fixture.write("expected.ppm", quadrants(240, 320, 60, 120), 240, 320);

        WHEN("the scenario runs") {
            const auto report = fixture.run("expected.ppm");
            THEN("it passes, and the report says how many pixels differ: none") {
                REQUIRE(report.verdict == scenario::Verdict::Pass);
                REQUIRE(report.screens.size() == 1);
                CHECK(report.screens[0].differing == 0);
                CHECK(report.screens[0].width == 240);
                CHECK(report.steps[1].kind == "screen");
            }
            THEN("the picture is one of the inputs, identified by its hash") {
                const auto image = std::ranges::find(report.inputs, std::string{"image"}, &scenario::Input::role);
                REQUIRE(image != report.inputs.end());
                CHECK(image->path == "expected.ppm");
                CHECK(image->sha256.size() == 64);
            }
        }
    }
    GIVEN("a picture whose green and blue quadrants are exchanged") {
        ScreenScenario fixture;
        auto           expected = quadrants(240, 320, 60, 120);
        for (auto& pixel : expected)
            pixel = pixel == 0x00FF00u ? 0x0000FFu : pixel == 0x0000FFu ? 0x00FF00u : pixel;
        fixture.write("diverging.ppm", expected, 240, 320);

        WHEN("the scenario runs") {
            const auto report = fixture.run("diverging.ppm");
            THEN("it fails, and says where the first difference is") {
                REQUIRE(report.verdict == scenario::Verdict::Fail);
                REQUIRE(report.screens.size() == 1);
                CHECK(report.screens[0].differing == 4800);
                CHECK(report.screens[0].firstX == 120);
                CHECK(report.screens[0].firstY == 120);
                CHECK_FALSE(report.screens[0].passed);
                CHECK(report.steps[1].outcome == scenario::StepRecord::Outcome::Failed);
                CHECK(report.steps[1].detail.find("(120, 120)") != std::string::npos);
                CHECK(scenario::exitCode(report.verdict) == 1);
            }
        }
    }
    GIVEN("pictures the screen cannot match") {
        ScreenScenario fixture;
        fixture.write("small.ppm", std::vector<std::uint32_t>(4, 0u), 2, 2);

        THEN("one of the wrong size is refused before the run, as beyond what the screen shows") {
            const auto report = fixture.run("small.ppm");
            REQUIRE(report.verdict == scenario::Verdict::Incompatible);
            REQUIRE(report.problem);
            CHECK(report.problem->cause == scenario::Problem::Cause::Capacity);
            CHECK(report.problem->path == "/steps/1/screen/image");
        }
        THEN("a missing file is refused as unreadable") {
            const auto report = fixture.run("absent.ppm");
            REQUIRE(report.problem);
            CHECK(report.problem->cause == scenario::Problem::Cause::Unreadable);
        }
        THEN("a component that is not a display is refused as a missing resource") {
            const auto report = fixture.run("small.ppm", "xtal");
            REQUIRE(report.problem);
            CHECK(report.problem->cause == scenario::Problem::Cause::Resource);
        }
    }
}
