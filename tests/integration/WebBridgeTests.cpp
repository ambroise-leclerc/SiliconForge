#include "../../apps/siliconforge/WebBridge.hpp"
#include <webfront/testing/Bdd.hpp>
#include "../support/Approx.hpp"
#include <siliconforge/arch/avr/Device.hpp>
#include <siliconforge/circuit/Description.hpp>
#include <siliconforge/mcu/Description.hpp>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <memory>
#include <type_traits>

using namespace webfront::bdd;

using namespace siliconforge;

namespace {
struct Frames {
    std::mutex mutex;
    std::condition_variable ready;
    std::vector<std::string> values;
    std::function<void()> beforeSend;

    std::vector<std::string> waitFor(std::size_t count) {
        std::unique_lock lock{mutex};
        ready.wait_for(lock, std::chrono::seconds{3}, [&] { return values.size() >= count; });
        return values;
    }
};

struct TestUI {
    std::shared_ptr<Frames> frames;
    webfront::WebLinkId id() const { return 1; }
    unsigned pendingWrites() const { return 0; }
    auto jsFunction(std::string_view) const {
        return [frames = frames](const std::string& json) {
            if (frames->beforeSend)
                frames->beforeSend();
            std::scoped_lock lock{frames->mutex};
            frames->values.push_back(json);
            frames->ready.notify_all();
        };
    }
};

struct TestApp {
    using UI = TestUI;
    std::function<void(UI)> started;
    std::function<void(std::string)> select;
    std::function<void(std::string)> loadMcu;
    std::function<void(std::string, std::vector<std::uint8_t>)> loadFirmware;
    std::function<std::vector<std::string>()> mcus;
    std::function<std::string()> chip;
    std::function<void()> reset;
    using Signals = std::tuple<double, double, double, double, std::vector<double>, std::vector<std::uint16_t>, std::vector<float>, std::vector<std::uint8_t>>;
    std::function<void(std::string)> selectCircuit;
    std::function<void(std::string)> loadExample;
    std::function<void(std::string, std::string)> loadCircuit;
    std::function<void(std::string, std::string)> moveComponent;
    std::function<void(std::string, std::string, double)> setComponent;
    std::function<Signals(double, double)> signals;
    std::function<std::vector<std::string>()> circuits;
    std::function<std::string()> circuit;
    template <typename F>
    void cppFunction(std::string_view name, F f) {
        if constexpr (std::is_invocable_v<F, std::string>) {
            if (name == "sf.selectMcu")
                select = std::move(f);
            else if (name == "sf.loadMcu")
                loadMcu = std::move(f);
            else if (name == "sf.selectCircuit")
                selectCircuit = std::move(f);
            else if (name == "sf.loadExample")
                loadExample = std::move(f);
        } else if constexpr (std::is_invocable_v<F, std::string, std::vector<std::uint8_t>>) {
            if (name == "sf.loadFirmware")
                loadFirmware = std::move(f);
        } else if constexpr (std::is_invocable_v<F, std::string, std::string, double>) {
            if (name == "sf.component.set")
                setComponent = std::move(f);
        } else if constexpr (std::is_invocable_v<F, std::string, std::string>) {
            if (name == "sf.loadCircuit")
                loadCircuit = std::move(f);
            else if (name == "sf.component.move")
                moveComponent = std::move(f);
        } else if constexpr (std::is_invocable_r_v<Signals, F, double, double>) {
            if (name == "sf.signals")
                signals = std::move(f);
        } else if constexpr (std::is_invocable_r_v<std::vector<std::string>, F>) {
            if (name == "sf.mcus")
                mcus = std::move(f);
            else if (name == "sf.circuits")
                circuits = std::move(f);
        } else if constexpr (std::is_invocable_r_v<std::string, F>) {
            if (name == "sf.chip")
                chip = std::move(f);
            else if (name == "sf.circuit")
                circuit = std::move(f);
        } else if constexpr (std::is_invocable_r_v<void, F>) {
            if (name == "sf.reset")
                reset = std::move(f);
        }
    }
    void onUIStarted(std::function<void(UI)> f) { started = std::move(f); }
    webfront::UISubscription subscribeUIClosed(std::function<void(UI)>) { return {}; }
};

class UntrustedNameDevice : public avr::Device {
public:
    UntrustedNameDevice() : Device(mcu::builtinDescription("atmega328p")) {}
    std::string_view name() const noexcept override { return "chip\xff"; }
};
}

const Scenario webStateReplacesInvalidUtf8In{"Web state replaces invalid UTF-8 in names, ELF symbols and paths", {"app", "bridge"}, [] {
    auto device = std::make_unique<UntrustedNameDevice>();
    ProgramImage image;
    image.symbols.emplace(0, "symbol\xff");
    device->load(image);
    device->serial()->transmit(0xff, 0);
    app::Simulation simulation{std::move(device)};
    TestApp web;
    auto frames = std::make_shared<Frames>();
    app::WebBridge bridge{web, simulation, "firmware\xff.hex"};
    web.started(TestUI{frames});
    const auto values = frames->waitFor(2);
    require(values.size() >= 2);  // The pusher survived and sent a subsequent frame.
    for (const auto& text : values) {
        check(std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 128; }));
        const auto state = nlohmann::json::parse(text);
        check(state["mcu"] == "chip\xef\xbf\xbd");
        check(state["symbol"] == "symbol\xef\xbf\xbd");
        check(state["firmware"] == "firmware\xef\xbf\xbd.hex");
    }
    check(nlohmann::json::parse(values.front())["serial"] == "\xc3\xbf");
}};

const Scenario mcuReplacementKeepsFrameMetadataCoherentAnd{"MCU replacement keeps frame metadata coherent and discards stale serial acknowledgements",
                                                           {"app", "bridge"}, [] {
    auto device = mcu::create("atmega328p");
    device->serial()->transmit('o', 0);
    device->serial()->transmit('l', 0);
    device->serial()->transmit('d', 0);
    app::Simulation simulation{std::move(device)};
    simulation.step();
    const auto previousStop = simulation.stopReason();
    require(previousStop);
    TestApp web;
    auto frames = std::make_shared<Frames>();
    bool replaced = false;
    app::WebBridge bridge{web, simulation, "old.hex", "AtMega328P"};
    frames->beforeSend = [&] {
        if (replaced)
            return;
        replaced = true;
        // Replace while the old frame is in flight, with its previous offset equal to zero.
        web.select("ATMEGA328P");
        simulation.access([](Mcu& mcu) {
            for (const unsigned char c : std::string_view{"replacement"})
                mcu.serial()->transmit(c, 0);
        });
    };
    web.started(TestUI{frames});
    const auto values = frames->waitFor(2);
    require(values.size() >= 2);
    const auto old = nlohmann::json::parse(values[0]);
    const auto next = nlohmann::json::parse(values[1]);
    check(old["firmware"] == "old.hex");
    check(old["mcuId"] == "atmega328p");
    check(old["stop"] == std::string(app::toString(*previousStop)));
    check(old["serial"] == "old");
    check(next["chipRevision"].get<unsigned>() == old["chipRevision"].get<unsigned>() + 1);
    check(next["firmware"] == "");
    check(next["mcuId"] == "atmega328p");
    check(next["stop"].is_null());
    check(next["serial"] == "replacement");
}};

const Scenario aResetDropsTheEdgesCountedBefore{"A reset drops the edges counted before it, even when no frame noticed the cycle counter going back",
                                                {"app", "bridge"}, [] {
    app::Simulation simulation{mcu::create("atmega328p")};
    TestApp web;
    app::WebBridge bridge{web, simulation, ""};
    // Before any frame the activity window starts at cycle 0 and the reset leaves the cycle counter at 0: only an
    // explicit reset of the activity can tell the two runs apart.
    simulation.access([](Mcu& mcu) {
        auto* pin = mcu.pin("PB0");
        pin->setExternal(true);
        pin->setExternal(false);
        pin->setExternal(true);
    });
    web.reset();
    auto frames = std::make_shared<Frames>();
    web.started(TestUI{frames});
    const auto values = frames->waitFor(1);
    require(!values.empty());
    const auto state = nlohmann::json::parse(values[0]);
    const auto pin   = std::ranges::find_if(state["pins"], [](const nlohmann::json& p) { return p["name"] == "PB0"; });
    require(pin != state["pins"].end());
    check((*pin)["edges"] == 0);
    check(state["cycles"] == 0);
}};

const Scenario serialPreservesEveryByteAndSequencesCrossing{"Serial preserves every byte and sequences crossing a frame boundary", {"app", "bridge"}, [] {
    auto device = mcu::create("atmega328p");
    // Split C3 A9 across the per-frame boundary. Neither byte may be decoded or replaced.
    constexpr auto frameLimit = app::WebBridge<TestApp>::maxSerialPerFrame;
    std::string bytes(frameLimit - 1, 'x');
    bytes += "\xc3\xa9";
    std::string escaped = "\"";
    for (unsigned byte = 0; byte <= 255; ++byte) {
        bytes += static_cast<char>(byte);
        escaped += std::format("\\u{:04x}", byte);
    }
    escaped += '"';
    for (const unsigned char byte : bytes)
        device->serial()->transmit(byte, 0);
    app::Simulation simulation{std::move(device)};
    TestApp web;
    auto frames = std::make_shared<Frames>();
    app::WebBridge bridge{web, simulation, ""};
    web.started(TestUI{frames});
    const auto values = frames->waitFor(2);
    require(values.size() >= 2);
    const auto first = nlohmann::json::parse(values[0])["serial"].get<std::string>();
    const auto second = nlohmann::json::parse(values[1])["serial"].get<std::string>();
    check(first == std::string(frameLimit - 1, 'x') + "\xc3\x83");  // U+00C3
    check(second == "\xc2\xa9" + nlohmann::json::parse(escaped).get<std::string>());  // U+00A9 then all 256 bytes.
    check(values[1].find("\\u00b0") != std::string::npos);  // Latin-1 degree sign is retained.
}};

const Scenario aFailedMcuInitializerPreservesTheCurrent{"A failed MCU initializer preserves the current simulation", {"app", "simulation"}, [] {
    auto device = mcu::create("atmega328p");
    const auto* original = device.get();
    app::Simulation simulation{std::move(device)};
    simulation.step();
    const auto previousStop = simulation.stopReason();
    const auto previousCycles = simulation.access([](Mcu& mcu) { return mcu.cycles(); });
    checkThrows<std::runtime_error>([&] { simulation.replace(mcu::create("atmega328p"), [](Mcu&) {
        throw std::runtime_error("initializer failed");
    }); });
    simulation.snapshot([&](Mcu& mcu, std::optional<StopReason> reason) {
        check(&mcu == original);
        check(mcu.cycles() == previousCycles);
        check(reason == previousStop);
    });
}};

namespace {
/// Calls `action`, which must reject with a BridgeError, and returns it.
template <typename Action>
webfront::BridgeError rejection(Action&& action) {
    try {
        action();
    } catch (const webfront::BridgeError& error) {
        return error;
    }
    fail("no BridgeError was thrown");
    throw std::logic_error{"unreachable"};
}
}  // namespace

const Scenario sfMcusReturnsTheCatalogAsA{"sf.mcus returns the catalog as a list", {"app", "bridge"}, [] {
    app::Simulation simulation{mcu::create("atmega328p")};
    TestApp web;
    app::WebBridge bridge{web, simulation, ""};
    const auto names = web.mcus();
    check(std::ranges::find(names, std::string{"atmega328p"}) != names.end());
}};

const Scenario mcuAndFirmwareLoadingErrorsCarryA{"MCU and firmware loading errors carry a typed code", {"app", "bridge"}, [] {
    app::Simulation simulation{mcu::create("atmega328p")};
    TestApp web;
    app::WebBridge bridge{web, simulation, ""};

    const auto unknown = rejection([&] { web.select("no-such-mcu"); });
    check(unknown.code() == app::error_code::unknownMcu);
    check(std::string{unknown.what()} == "Unknown MCU: no-such-mcu");

    auto broken = mcu::builtinDescription("atmega328p");
    broken["clockHz"] = "fast";
    const auto invalidMcu = rejection([&] { web.loadMcu(broken.dump()); });
    check(invalidMcu.code() == app::error_code::invalidMcu);
    check(std::string{invalidMcu.what()}.contains("clockHz"));  // The JSON path leads the message.

    const std::string badHex = ":10000000ZZ\n";
    const auto invalidFirmware = rejection([&] { web.loadFirmware("bad.hex", {badHex.begin(), badHex.end()}); });
    check(invalidFirmware.code() == app::error_code::invalidFirmware);
    check(std::string{invalidFirmware.what()}.contains("(line 1)"));

    // A rejected load leaves the simulation as it was.
    check(simulation.access([](Mcu& mcu) { return std::string{mcu.name()}; }) == "ATmega328P");
}};

const Scenario aDescriptionAndAChipLargerThan{"A description and a chip larger than 64 KiB cross the bridge functions", {"app", "bridge"}, [] {
    app::Simulation simulation{mcu::create("atmega328p")};
    TestApp web;
    app::WebBridge bridge{web, simulation, ""};
    auto description = mcu::builtinDescription("atmega328p");
    const std::string name(100 * 1024, 'n');
    description["name"] = name;
    requireNothrow([&] { web.loadMcu(description.dump()); });
    const auto chip = web.chip();
    check(chip.size() > 64 * 1024);
    check(chip.contains(name));
}};

const Scenario theBridgePlugsTheChipInA{"The bridge plugs the chip in a circuit and pushes the state of its components", {"app", "bridge", "circuit"}, [] {
    app::Simulation simulation{mcu::create("atmega328p")};
    TestApp web;
    app::WebBridge bridge{web, simulation, "", "atmega328p", circuit::builtinDescription("arduino-demo"), "arduino-demo"};
    const auto view = nlohmann::json::parse(web.circuit());
    check(view.at("id") == "arduino-demo");
    check(view.at("components").size() == 7);
    const auto builtin = web.circuits();
    check(std::ranges::find(builtin, std::string{"seven-segment"}) != builtin.end());

    auto frames = std::make_shared<Frames>();
    web.started(TestUI{frames});
    require(frames->waitFor(1).size() >= 1);
    auto state = nlohmann::json::parse(frames->waitFor(1).front());
    check(state.at("components").at("led-d13").at("brightness") == 0.0);
    check(state.at("components").at("button-d2").at("pressed") == false);
    check(state.at("shorts").empty());
    const auto revision = state.at("circuitRevision").get<unsigned>();

    section("an interaction goes through the electrical model", [&] {
        web.setComponent("button-d2", "pressed", 1);
        check(simulation.access([](Mcu& mcu) { return mcu.pin("D2")->state(); }) == PinState::Low);
        web.setComponent("pot-a0", "position", 0.5);
        check(simulation.access([](Mcu& mcu) { return mcu.pin("A0")->voltage(5.0); }) == siliconforge::test::Approx(2.5).margin(1e-3));

        check(rejection([&] { web.setComponent("no-such-part", "pressed", 1); }).code() == app::error_code::unknownComponent);
        check(rejection([&] { web.setComponent("button-d2", "depth", 1); }).code() == app::error_code::unknownComponent);
        check(rejection([&] { web.setComponent("led-d13", "brightness", 1); }).code() == app::error_code::invalidValue);
        const auto bounds = rejection([&] { web.setComponent("pot-a0", "position", 2); });
        check(bounds.code() == app::error_code::invalidValue);
        check(std::string{bounds.what()}.contains("out of bounds"));
    });
    section("a component moved by the user rebuilds the circuit and keeps what the user set", [&] {
        web.setComponent("pot-a0", "position", 0.5);
        web.moveComponent("r-d13", R"({"a": "i15", "b": "i22"})");
        web.moveComponent("wire-7", R"({"from": "b10"})");
        const auto moved = nlohmann::json::parse(web.circuit());
        check(moved.at("id") == "arduino-demo");
        check(moved.at("components")[1].at("at") == nlohmann::json{{"a", "i15"}, {"b", "i22"}});
        check(moved.at("wires")[6].at("from") == "b10");
        check(moved.at("types").at("resistor").at("footprint").at("maximum") == 12);
        check(moved.at("types").at("wire").at("footprint").at("maximum") == 24);
        check(simulation.access([](Mcu& mcu) { return mcu.pin("A0")->voltage(5.0); }) == siliconforge::test::Approx(2.5).margin(1e-3));
        const auto count = frames->waitFor(0).size();
        check(nlohmann::json::parse(frames->waitFor(count + 2).back()).at("circuitRevision").get<unsigned>() > revision);

        const auto tooLong = rejection([&] { web.moveComponent("r-d13", R"({"b": "i28"})"); });
        check(tooLong.code() == app::error_code::invalidPlacement);
        check(std::string{tooLong.what()}.starts_with("/components/1/at: "));
        check(rejection([&] { web.moveComponent("no-such-part", R"({"a": "h1"})"); }).code() == app::error_code::unknownComponent);
        check(rejection([&] { web.moveComponent("r-d13", "{"); }).code() == app::error_code::invalidPlacement);
        check(nlohmann::json::parse(web.circuit()).at("components")[1].at("at").at("b") == "i22");
    });
    section("another circuit is selected, or loaded from its text", [&] {
        web.selectCircuit("led-buttons");
        check(nlohmann::json::parse(web.circuit()).at("id") == "led-buttons");
        auto mine    = circuit::builtinDescription("led-buttons");
        mine["name"] = "Mine";
        web.loadCircuit("mine.json", mine.dump());
        check(nlohmann::json::parse(web.circuit()).at("name") == "Mine");
        const auto count = frames->waitFor(0).size();
        state            = nlohmann::json::parse(frames->waitFor(count + 2).back());
        check(state.at("circuitRevision").get<unsigned>() > revision);
        check(state.at("components").contains("led-d9"));
    });
    section("an invalid circuit is refused with its JSON path and the current one stays", [&] {
        auto broken = circuit::builtinDescription("led-buttons");
        broken["components"][1]["properties"]["resistance"] = -5;
        const auto invalid = rejection([&] { web.loadCircuit("broken.json", broken.dump()); });
        check(invalid.code() == app::error_code::invalidCircuit);
        check(std::string{invalid.what()}.starts_with("/components/1/properties/resistance: "));
        check(rejection([&] { web.loadCircuit("text.json", "{"); }).code() == app::error_code::invalidCircuit);
        check(rejection([&] { web.selectCircuit("no-such-circuit"); }).code() == app::error_code::unknownCircuit);
        auto elsewhere             = circuit::builtinDescription("led-buttons");
        elsewhere["chip"]["mcu"]   = "no-such-mcu";
        const auto unknownMcu = rejection([&] { web.loadCircuit("elsewhere.json", elsewhere.dump()); });
        check(std::string{unknownMcu.what()}.starts_with("/chip/mcu: "));
        check(nlohmann::json::parse(web.circuit()).at("id") == "arduino-demo");
    });
    section("a chip the circuit was not made for gets a bare board", [&] {
        auto other    = mcu::builtinDescription("atmega328p");
        other["name"] = "Another chip";
        web.loadMcu(other.dump());
        check(web.circuit() == "null");
        web.select("atmega328p");
        check(nlohmann::json::parse(web.circuit()).at("id") == "arduino-demo");
    });
}};

const Scenario anExampleLoadsItsCircuitAndIts{"An example loads its circuit and its firmware, and the capture restarts with it",
                                              {"app", "bridge", "instruments"}, [] {
    app::Simulation simulation{mcu::create("atmega328p")};
    TestApp web;
    app::WebBridge bridge{web, simulation, ""};
    check(web.circuit() == "null");
    check(rejection([&] { web.loadExample("no-such-example"); }).code() == app::error_code::unknownExample);

    const auto before = std::get<0>(web.signals(-1, 0));
    web.loadExample("blink");
    simulation.pause();
    check(nlohmann::json::parse(web.circuit()).at("id") == "arduino-demo");

    // 1.6 simulated seconds of blink: D13 rises at once, falls after 0.5 s, rises after 1 s, falls after 1.5 s.
    simulation.access([](Mcu& mcu) { mcu.run(mcu.cyclesFor(std::chrono::milliseconds{1600}) - mcu.cycles()); });
    const auto [epoch, first, next, now, at, channel, volts, state] = web.signals(-1, 0);
    check(epoch > before);
    check(first == 0);
    check(next == static_cast<double>(at.size()));
    check(now >= 1.6 * 16e6);
    const auto d13 = simulation.access([](Mcu& mcu) { return static_cast<std::uint16_t>(mcu.pin("D13") - mcu.pins().data()); });
    std::vector<double> rises, falls;
    for (std::size_t i = 0; i < at.size(); ++i)
        if (channel[i] == d13 && at[i] > 0)
            (state[i] == 1 ? rises : falls).push_back(at[i]);
    require(rises.size() == 2);
    require(falls.size() == 2);
    check(falls[0] - rises[0] == siliconforge::test::Approx(8e6).epsilon(0.001));  // Half a second, in cycles
    check(rises[1] - rises[0] == siliconforge::test::Approx(16e6).epsilon(0.001));
    for (std::size_t i = 0; i < at.size(); ++i)
        if (channel[i] == d13)
            check(volts[i] == (state[i] == 1 ? 5.0f : 0.0f));

    section("a reader that is up to date gets nothing, and a stale epoch starts again", [&] {
        check(std::get<4>(web.signals(epoch, next)).empty());
        check(std::get<4>(web.signals(epoch - 1, next)).size() == at.size());
    });
    section("a reset restarts the capture", [&] {
        web.reset();
        const auto restarted = web.signals(epoch, next);
        check(std::get<0>(restarted) == epoch + 1);
        check(std::get<3>(restarted) == 0);
        check(std::get<4>(restarted).size() == simulation.access([](Mcu& mcu) { return mcu.pins().size(); }));  // One starting point per pin
    });
}};
