// Character LCD (LCD1602, HD44780): the controller driven wire by wire for the cases a firmware does not produce, then the
// lcd1602 firmware on the demonstration circuit, which reads the busy flag back, and a bench scenario that checks the text.

#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/circuit/Hd44780.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/scenario/Scenario.hpp>
#include <siliconforge/testing/Bench.hpp>

#include <webfront/testing/Bdd.hpp>

using namespace webfront::bdd;

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

using namespace siliconforge;
using namespace siliconforge::circuit;
using namespace std::chrono_literals;
using Json = nlohmann::json;

namespace {
const std::filesystem::path firmwareDir{SILICONFORGE_FIRMWARE_DIR};
const std::filesystem::path circuitDir{std::filesystem::path{SILICONFORGE_SOURCE_DIR} / "circuits"};

/// Drives the wires of a standalone controller, 1 µs per level: slow enough for every timing of the datasheet.
struct Wires {
    Hd44780& lcd;
    double   now{0.020};  // Past the internal reset of 10 ms
    bool     fourBit{false};

    explicit Wires(Hd44780& target) : lcd(target) { lcd.power(true, 0); }
    void level(bool rs, bool rw, bool e, std::uint8_t data) {
        now += 1e-6;
        lcd.bus(rs, rw, e, data, now);
    }
    void strobe(bool rs, std::uint8_t data) {
        level(rs, false, false, data);
        level(rs, false, true, data);
        level(rs, false, false, data);
    }
    /// One byte, in one transfer or two; then waits for the instruction to end unless `wait` is false.
    void send(bool rs, std::uint8_t value, bool wait = true) {
        if (fourBit) {
            strobe(rs, value & 0xF0);
            strobe(rs, static_cast<std::uint8_t>(value << 4));
        } else {
            strobe(rs, value);
        }
        if (wait)
            now += 2e-3;
    }
    void command(std::uint8_t value, bool wait = true) { send(false, value, wait); }
    void print(std::string_view text) {
        for (const char c : text)
            send(true, static_cast<std::uint8_t>(c));
    }
    /// A read cycle: what the module drives while E is high, the byte in 8-bit mode, the two nibbles in 4-bit mode.
    std::uint8_t read(bool rs) {
        const auto pulse = [&] {
            level(rs, true, false, 0);
            level(rs, true, true, 0);
            const auto output = lcd.output();
            level(rs, true, false, 0);
            return output;
        };
        const auto first = pulse();
        require(first.has_value());
        if (!fourBit) {
            check(first->mask == 0xFF);
            return first->value;
        }
        const auto second = pulse();
        require(second.has_value());
        check(first->mask == 0xF0);
        return static_cast<std::uint8_t>((first->value & 0xF0) | second->value >> 4);
    }
    /// The initialisation by instruction of the datasheet, into 4-bit mode with two lines, display on.
    void initialiseFourBit() {
        strobe(false, 0x30);
        now += 5e-3;
        strobe(false, 0x30);
        now += 2e-4;
        strobe(false, 0x30);
        now += 2e-4;
        strobe(false, 0x20);
        now += 2e-4;
        fourBit = true;
        command(0x28);
        command(0x0C);
        command(0x01);
        command(0x06);
    }
};

bool lcd1602Blank(const Hd44780& lcd) {
    return lcd.text() == std::vector<std::string>{std::string(16, ' '), std::string(16, ' ')};
}

testing::Bench runDemo() {
    const auto path = firmwareDir / "lcd1602.hex";
    if (!std::filesystem::exists(path))
        skip(path.string(), " is missing: build the firmware (see firmware/README.md)");
    testing::Bench bench{mcu::create("atmega328p")};
    bench.attach(Json::parse(std::ifstream{circuitDir / "lcd1602-demo.json"}));
    bench.load(path);
    require(bench.runUntil([&] { return bench.pin("D12").level(); }, 1s, 4096));
    return bench;
}
}  // namespace

const Scenario anHd44780TakesItsInstructions{"an HD44780 takes its instructions and characters from the wires", {"circuit", "lcd"}, [] {
    given("a controller just powered", [&] {
        Hd44780 lcd;
        Wires   wires{lcd};

        then("it is in its internal reset: 8-bit interface, one line, display off, blank", [&] {
            check(lcd.eightBit());
            check(!lcd.twoLines());
            check(!lcd.displayOn());
            check(lcd.text() == std::vector<std::string>{std::string(16, ' '), std::string(16, ' ')});
        });
        when("it is initialised in 8-bit mode and given text", [&] {
            wires.command(0x38);
            wires.command(0x0C);
            wires.command(0x06);
            wires.print("Hello");
            wires.command(0xC0);
            wires.print("World");

            then("the text is on the two rows", [&] {
                check(lcd.text()[0] == "Hello           ");
                check(lcd.text()[1] == "World           ");
                check(lcd.instructions() == 4);
                check(lcd.dataWrites() == 10);
                check(lcd.early() == 0);
                check(lcd.timing() == 0);
            });
        });
        when("an instruction comes while the controller still clears the display", [&] {
            wires.command(0x38);
            wires.command(0x0C);
            wires.print("X");
            wires.command(0x01, false);  // A clear takes 1.52 ms
            wires.send(true, 'Y');       // 3 µs after the clear: lost
            wires.send(true, 'Z');       // 2 ms after the clear: taken

            then("the write is lost and counted", [&] {
                check(lcd.early() == 1);
                check(lcd.text()[0] == "Z               ");
            });
        });
        when("the clock of the MCU starts again while the module stays powered", [&] {
            wires.command(0x38);
            wires.command(0x0C);
            wires.command(0x01, false);  // A clear under way: 1.52 ms
            wires.now = 0;               // The MCU was reset
            wires.send(true, 'A');       // 3 µs later on the new clock: still clearing
            wires.send(true, 'B');       // 2 ms later: done

            then("the instruction under way still runs its time, and no pulse is taken as too close", [&] {
                check(lcd.early() == 1);
                check(lcd.timing() == 0);
                check(lcd.text()[0].starts_with("B"));
            });
        });
        when("E is pulsed for less than 230 ns", [&] {
            wires.command(0x38);
            lcd.bus(false, false, true, 0x0C, wires.now += 1e-6);
            lcd.bus(false, false, false, 0x0C, wires.now += 125e-9);

            then("the instruction is executed, and the pulse counted as a timing violation", [&] {
                check(lcd.displayOn());
                check(lcd.timing() == 1);
            });
        });
    });
    given("a controller initialised in 4-bit mode by instruction", [&] {
        Hd44780 lcd;
        Wires   wires{lcd};
        wires.initialiseFourBit();

        then("it is in 4-bit mode with two lines, and nothing was lost", [&] {
            check(!lcd.eightBit());
            check(lcd.twoLines());
            check(lcd.displayOn());
            check(lcd.early() == 0);
        });
        when("a character is defined in CGRAM and shown", [&] {
            wires.command(0x48);  // Character 1
            for (const auto row : std::initializer_list<std::uint8_t>{0x00, 0x0A, 0x1F, 0x1F, 0x0E, 0x04, 0x00, 0x00})
                wires.send(true, row);
            wires.command(0x80);
            wires.send(true, 0x01);
            wires.send(true, 0x09);  // 8 to 15 are 0 to 7 again
            wires.print("\\~");

            then("its dots come from CGRAM, and the text gives it as U+0001", [&] {
                check(lcd.glyph(0x01)[2] == 0x1F);
                check(lcd.glyph(0x09) == lcd.glyph(0x01));
                check(lcd.text()[0] == std::string{"\x01\x01¥→"} + std::string(12, ' '));
            });
        });
        when("the busy flag and the address counter are read just after a write", [&] {
            wires.command(0x80 + 5);
            wires.send(true, 'A', false);
            const auto status = wires.read(false);

            then("the controller is busy and the address moved past the character", [&] {
                check((status & 0x80) != 0);
                check((status & 0x7F) == 6);
            });
            then("once the write is done the flag clears, and the character can be read back", [&] {
                wires.now += 1e-3;
                check((wires.read(false) & 0x80) == 0);
                wires.command(0x80 + 5);
                check(wires.read(true) == 'A');
                check((wires.read(false) & 0x7F) == 6);  // Reading moved the address counter too
            });
        });
        when("text runs past the end of the first line", [&] {
            wires.command(0x80 + 0x26);
            wires.print("abc");

            then("it goes on at the start of the second line", [&] {
                check(lcd.ddram()[38] == 'a');
                check(lcd.ddram()[39] == 'b');
                check(lcd.text()[1].starts_with("c"));
            });
        });
        when("the display is shifted left twice", [&] {
            wires.print("0123456789ABCDEFGH");
            wires.command(0x18);
            wires.command(0x18);

            then("the rows show the memory from the third column", [&] {
                check(lcd.text()[0] == "23456789ABCDEFGH");
                check(lcd.shift() == 2);
            });
            then("a return home puts it back", [&] {
                wires.command(0x02);
                check(lcd.text()[0] == "0123456789ABCDEF");
                check(lcd.addressCounter() == 0);
            });
        });
        when("the cursor and blink are turned on", [&] {
            wires.command(0x80 + 0x43);
            wires.command(0x0F);

            then("the cursor is on its cell, which the text leaves out", [&] {
                check(lcd.cursorOn());
                check(lcd.blinkOn());
                const auto cell = lcd.cursorCell();
                require(cell.has_value());
                check(cell->first == 3);
                check(cell->second == 1);
            });
        });
    });
    given("the characters of ROM A00 beyond ASCII", [&] {
        then("they are given as they look", [&] {
            check(Hd44780::character(0x5C) == "¥");
            check(Hd44780::character(0xB1) == "ｱ");
            check(Hd44780::character(0xDF) == "ﾟ");
            check(Hd44780::character(0xE4) == "μ");
            check(Hd44780::character(0xF4) == "Ω");
            check(Hd44780::character(0xFF) == "█");
            check(Hd44780::character(0x80).empty());
        });
    });
}};

const Scenario aFirmwareWritesAnLcd1602{"a firmware writes an LCD1602 on a 4-bit bus and reads its busy flag", {"circuit", "lcd"}, [] {
    given("the lcd1602 firmware on the demonstration circuit, run until it raises D12", [&] {
        auto        bench = runDemo();
        const auto* lcd   = bench.circuit().characterDisplay("lcd");
        require(lcd != nullptr);

        then("the module shows the greeting, the heart from CGRAM and the seconds", [&] {
            const auto text = lcd->text();
            check(text[0] == std::string{"Hello, world! "} + '\0' + ' ');
            check(text[1] == "Seconds: 0      ");
        });
        then("the firmware polled the busy flag instead of waiting, and broke no timing", [&] {
            check(lcd->reads() > 0);
            check(lcd->early() == 0);
            check(lcd->timing() == 0);
            check(!lcd->eightBit());
        });
        then("the potentiometer at its end gives a full contrast, and the backlight is lit", [&] {
            check(bench.circuit().state("lcd", "contrast").value_or(0) == 1.0);
            check(bench.circuit().state("lcd", "backlight").value_or(0) > 0.9);
            check(bench.circuit().state("lcd", "dataBits").value_or(0) == 4);
        });
        then("the view gets the dots of every cell and the text", [&] {
            const auto shown = bench.circuit().screenImage("lcd");
            check(shown.at("columns") == 16);
            check(shown.at("rows") == 2);
            check(shown.at("on") == true);
            check(shown.at("cells").size() == 32);
            check(shown.at("cells")[14][1] == 0x0A);  // The heart, second row of dots
            check(shown.at("cursor").is_null());
            check(shown.at("text")[1] == "Seconds: 0      ");
        });
        when("a second goes by", [&] {
            bench.runFor(1050ms);
            then("the counter shows it", [&] { check(lcd->text()[1] == "Seconds: 1      "); });
        });
    });
}};

namespace {
/// A scenario of the LCD demo that expects `rows` once D12 is high.
scenario::Report runTextScenario(const Json& rows, const std::string& component = "lcd") {
    const Json document = {
        {"formatVersion", 1},
        {"name", "lcd"},
        {"circuit", (circuitDir / "lcd1602-demo.json").string()},
        {"firmware", (firmwareDir / "lcd1602.hex").string()},
        {"steps", {{{"waitFor", {{"pin", "D12"}, {"state", "high"}}}, {"timeout", "500ms"}},
                   {{"screen", {{"component", component}, {"text", rows}}}}}}};
    auto prepared = scenario::Scenario::prepare(document.dump(), std::filesystem::temp_directory_path(), "lcd.json");
    if (!prepared) {
        scenario::Report report;
        report.verdict = scenario::Verdict::Incompatible;
        report.problem = prepared.error();
        return report;
    }
    return (*prepared)->run();
}
}  // namespace

const Scenario aScenarioChecksTheTextOfAnLcd{"a bench scenario checks the text of a character LCD", {"scenario", "lcd"}, [] {
    if (!std::filesystem::exists(firmwareDir / "lcd1602.hex"))
        skip("lcd1602.hex is missing: build the firmware (see firmware/README.md)");
    given("the rows the firmware writes, without their trailing spaces", [&] {
        const auto report = runTextScenario(Json::array({std::string{"Hello, world! "} + '\0', "Seconds: 0"}));
        then("it passes, and the report gives what was shown", [&] {
            require(report.verdict == scenario::Verdict::Pass);
            require(report.screens.size() == 1);
            check(report.screens[0].differing == 0);
            check(report.screens[0].shown[1] == "Seconds: 0");
            check(report.screens[0].width == 16);
        });
    });
    given("only the first row", [&] {
        const auto report = runTextScenario(Json::array({std::string{"Hello, world! "} + '\0'}));
        then("the second is expected blank, and it is not", [&] {
            require(report.verdict == scenario::Verdict::Fail);
            check(report.screens[0].firstY == 1);
            check(report.screens[0].firstX == 0);
        });
    });
    given("a row that differs from its eighth character", [&] {
        const auto report = runTextScenario(Json::array({std::string{"Hello, World! "} + '\0', "Seconds: 0"}));
        then("it fails and says where", [&] {
            require(report.verdict == scenario::Verdict::Fail);
            check(report.screens[0].differing == 1);
            check(report.screens[0].firstX == 7);
            check(report.screens[0].firstY == 0);
            check(report.steps[1].detail.find("\\u0000") != std::string::npos);
        });
    });
    given("text the display cannot hold", [&] {
        then("three rows are refused before the run", [&] {
            const auto report = runTextScenario(Json::array({"a", "b", "c"}));
            require(report.problem);
            check(report.problem->cause == scenario::Problem::Cause::Schema);
        });
        then("a row of 17 characters is refused as beyond what the display shows", [&] {
            const auto report = runTextScenario(Json::array({"12345678901234567"}));
            require(report.problem);
            check(report.problem->cause == scenario::Problem::Cause::Capacity);
            check(report.problem->path == "/steps/1/screen/text/0");
        });
        then("a component that is not a character LCD is refused as a missing resource", [&] {
            const auto report = runTextScenario(Json::array({"a"}), "contrast");
            require(report.problem);
            check(report.problem->cause == scenario::Problem::Cause::Resource);
        });
    });
}};

const Scenario aFirmwareWritesAnLcdThroughI2c{"a firmware writes an LCD1602 through its I2C backpack with the TWI", {"circuit", "lcd", "twi"}, [] {
    const auto path = firmwareDir / "lcd1602_i2c.hex";
    if (!std::filesystem::exists(path))
        skip(path.string(), " is missing: build the firmware (see firmware/README.md)");
    const auto description = Json::parse(std::ifstream{circuitDir / "lcd1602-i2c-demo.json"});
    given("the lcd1602_i2c firmware on the demonstration circuit, run until it raises D12", [&] {
        testing::Bench bench{mcu::create("atmega328p")};
        bench.attach(description);
        bench.load(path);
        require(bench.runUntil([&] { return bench.pin("D12").level() || bench.pin("D13").level(); }, 2s, 4096));
        const auto* lcd = bench.circuit().characterDisplay("lcd");
        require(lcd != nullptr);

        then("every status of the bus was the expected one, and the module shows the text", [&] {
            check(!bench.pin("D13").level());
            check(lcd->text()[0] == "Hello over I2C! ");
            check(lcd->text()[1] == "Seconds: 0      ");
        });
        then("the busy flag was read through the PCF8574, and nothing was lost or too fast", [&] {
            check(lcd->reads() > 0);
            check(bench.circuit().state("lcd", "i2cReads").value_or(0) > 0);
            check(lcd->early() == 0);
            check(lcd->timing() == 0);
            check(!lcd->eightBit());
        });
        then("P3 lights the backlight, and the trimmer gives the contrast", [&] {
            check(bench.circuit().state("lcd", "backlight").value_or(0) == 1.0);
            check(bench.circuit().state("lcd", "contrast").value_or(0) == 1.0);
            require(bench.circuit().set("lcd", "trimmer", 1.0).has_value());
            bench.runFor(1ms);  // The next write of the firmware observes it
            bench.runUntil([&] { return bench.circuit().state("lcd", "contrast").value_or(1) == 0.0; }, 1100ms, 4096);
            check(bench.circuit().state("lcd", "contrast").value_or(1) == 0.0);
        });
    });
    given("a second module at 0x26 on the same bus, after the first in the circuit", [&] {
        auto shared = description;
        shared["board"]["columns"] = 36;
        shared["components"].push_back({{"id", "lcd2"}, {"type", "lcd1602-i2c"}, {"properties", {{"address", 0x26}}},
                                        {"at", {{"gnd", "j31"}, {"vcc", "j32"}, {"sda", "j33"}, {"scl", "j34"}}}});
        for (const auto& [from, to] : {std::pair{"h31", "T-31"}, {"h32", "T+32"}, {"g28", "g33"}, {"g29", "g34"}})
            shared["wires"].push_back({{"from", from}, {"to", to}});
        testing::Bench bench{mcu::create("atmega328p")};
        bench.attach(shared);
        bench.load(path);
        require(bench.runUntil([&] { return bench.pin("D12").level() || bench.pin("D13").level(); }, 2s, 4096));
        then("the module that releases SDA does not hide the acknowledge of the other: the open-drain lines are a wired AND", [&] {
            check(!bench.pin("D13").level());
            check(bench.circuit().characterDisplay("lcd")->text()[0] == "Hello over I2C! ");
            check(lcd1602Blank(*bench.circuit().characterDisplay("lcd2")));
            check(bench.circuit().state("lcd2", "i2cWrites").value_or(-1) == 0);
        });
    });
    given("the same circuit with the backpack at 0x26", [&] {
        auto other = description;
        other["components"][0]["properties"]["address"] = 0x26;
        testing::Bench bench{mcu::create("atmega328p")};
        bench.attach(other);
        bench.load(path);
        require(bench.runUntil([&] { return bench.pin("D12").level() || bench.pin("D13").level(); }, 2s, 4096));
        then("the address is not acknowledged: the firmware stops on D13 and the LCD stays blank", [&] {
            check(bench.pin("D13").level());
            check(bench.circuit().state("lcd", "i2cWrites").value_or(-1) == 0);
            check(lcd1602Blank(*bench.circuit().characterDisplay("lcd")));
        });
    });
}};
