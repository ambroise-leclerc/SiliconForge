#pragma once

#include "Simulation.hpp"

#include "EmbeddedFirmware.hpp"

#include <nlohmann/json.hpp>
#include <siliconforge/circuit/Circuit.hpp>
#include <siliconforge/core/loader/ProgramImage.hpp>
#include <siliconforge/instruments/SignalRecorder.hpp>
#include <siliconforge/mcu/Registry.hpp>

#include <webfront/WebFront.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace siliconforge::app {

/// Codes of the errors that `sf.*` functions reject their Promise with (`error.code` in the browser).
namespace error_code {
inline constexpr std::string_view unknownMcu       = "siliconforge.unknownMcu";        ///< sf.selectMcu: not in the catalog.
inline constexpr std::string_view invalidMcu       = "siliconforge.invalidMcu";        ///< sf.loadMcu: message is "<JSON path>: <reason>".
inline constexpr std::string_view invalidFirmware  = "siliconforge.invalidFirmware";   ///< sf.loadFirmware: unparsable image, message ends with "(line N)" when known.
inline constexpr std::string_view firmwareRejected = "siliconforge.firmwareRejected";  ///< sf.loadFirmware: the MCU refused the image.
inline constexpr std::string_view unknownCircuit   = "siliconforge.unknownCircuit";    ///< sf.selectCircuit: not a built-in circuit.
inline constexpr std::string_view invalidCircuit   = "siliconforge.invalidCircuit";    ///< sf.loadCircuit: message is "<JSON path>: <reason>".
inline constexpr std::string_view unknownComponent = "siliconforge.unknownComponent";  ///< sf.component.set, sf.component.move: no such component or state.
inline constexpr std::string_view invalidValue     = "siliconforge.invalidValue";      ///< sf.component.set: read-only state or value out of bounds.
inline constexpr std::string_view unknownExample   = "siliconforge.unknownExample";    ///< sf.loadExample: not in sf.examples.
inline constexpr std::string_view invalidPlacement = "siliconforge.invalidPlacement";  ///< sf.component.move: message is "<JSON path>: <reason>".
}  // namespace error_code

/// A firmware shipped with the application together with the circuit it was written for: one click in the interface.
struct Example {
    std::string_view id;
    std::string_view title;
    std::string_view circuit;
    std::string_view firmware;
    std::string_view description;
};
inline constexpr Example examples[] = {
    {"demo", "Blink + breathing LED", "arduino-demo", "demo.hex", "Red LED at 1 Hz on D13, green LED breathing with a 7.8 kHz PWM on D6"},
    {"counter", "7-segment counter", "seven-segment", "counter.hex", "Counts 0 to 9 on a 7-segment display and on the serial port"},
    {"buttons", "LED and buttons", "led-buttons", "buttons.hex", "Hold the D2 button to light the LED, press D3 to latch it"},
    {"uart_echo", "UART echo", "arduino-demo", "uart_echo.hex", "Echoes the lines of the serial monitor in upper case, 9600 baud"},
    {"pwm", "PWM 62.5 kHz", "arduino-demo", "pwm.hex", "Timer0 Fast PWM: 25 % on D6 and 75 % on D5"},
    {"blink", "Blink", "arduino-demo", "blink.hex", "The LED on D13 at 1 Hz"},
    {"timer0_isr", "Timer interrupt", "arduino-demo", "timer0_isr.hex", "A Timer0 interrupt service routine toggling a pin"},
};

/// Connects a Simulation to the browsers of a WebFront application.
///
/// The browser calls `sf.*` C++ functions to control the MCU; the bridge pushes the MCU state to every connected
/// browser about 30 times per second by calling `siliconForge.onState(json)`. Each pin carries the fraction of the
/// last frame it spent high, and each component of the circuit its state from the electrical model (ADR 0005), so
/// that a PWM-driven LED is displayed with its average brightness.
///
/// The bridge also owns the capture memory of the instruments: every pin change is recorded with its cycle, and the
/// page reads it with `sf.signals` as typed arrays.
template <typename App>
class WebBridge {
public:
    using UI = typename App::UI;

    /// `circuit` is the description of the circuit the chip is plugged in (already validated), null for a bare board.
    WebBridge(App& app, Simulation& simulation, std::string firmware, std::optional<std::string_view> catalogName = std::nullopt,
              nlohmann::json circuit = nullptr, std::string circuitLabel = {})
        : app(app), simulation(simulation), firmwareName(std::move(firmware)), mcuId(catalogName ? catalogId(*catalogName) : std::nullopt),
          description(std::move(circuit)), circuitName(std::move(circuitLabel)) {
        simulation.access([this](Mcu& mcu) {
            observe(mcu);
        });
        registerFunctions();
        serveRenderers();
        app.onUIStarted([this](UI ui) {
            std::scoped_lock lock{clientsMutex};
            clients.emplace(ui.id(), Client{ui, 0});
        });
        closed = app.subscribeUIClosed([this](UI ui) {
            std::scoped_lock lock{clientsMutex};
            clients.erase(ui.id());
        });
        pusher = std::jthread([this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds{33});
                pushState();
            }
        });
    }

    ~WebBridge() {
        pusher.request_stop();
        if (pusher.joinable())
            pusher.join();
        simulation.shutdown();  // The pin observers refer to this bridge: no instruction may run after it is gone.
    }

    /// Serial bytes sent in one frame; the rest follows in the next ones.
    static constexpr std::size_t maxSerialPerFrame = 64 * 1024;
    /// Samples returned by one call of sf.signals; the page calls again while it is behind.
    static constexpr std::size_t maxSignalsPerCall = 32 * 1024;
    static constexpr double      supplyVoltage     = 5.0;

private:
    /// High time of one pin over the current frame.
    struct Activity {
        Cycles windowStart{0};
        Cycles highSince{0};
        Cycles highTotal{0};
        u32    edges{0};  ///< Changes during the frame: two or more mean a PWM or a fast signal.
        bool   high{false};

        void change(bool nowHigh, Cycles at) {
            if (nowHigh == high)
                return;  // Floating <-> low is no edge of the level the LED shows.
            ++edges;
            if (high)
                highTotal += at - std::max(highSince, windowStart);
            high      = nowHigh;
            highSince = at;
        }

        /// What the frame ending at `now` reports for this pin.
        struct Sample {
            double duty;
            u32    edges;
        };

        /// Closes the frame ending at `now`. The reset is detected here, before the edge count is read, so that the
        /// first frame after a reset never carries edges of the previous run.
        Sample takeFrame(Cycles now) {
            if (now < windowStart) {  // Safety net: the MCU went back in time without restartActivity()
                windowStart = highSince = now;
                highTotal               = 0;
                edges                   = 0;
            }
            const auto counted = std::exchange(edges, 0u);  // An empty window must not carry them into the next one.
            if (now == windowStart)
                return {high ? 1.0 : 0.0, counted};
            const auto total = highTotal + (high ? now - std::max(highSince, windowStart) : 0);
            const auto duty  = static_cast<double>(total) / static_cast<double>(now - windowStart);
            windowStart      = now;
            highTotal        = 0;
            return {duty, counted};
        }
    };

    struct Client {
        UI          ui;
        std::size_t serialOffset;
    };

    void observe(Mcu& mcu) {
        std::vector<Activity> prepared(mcu.pins().size());
        for (std::size_t index = 0; index < mcu.pins().size(); ++index) {
            auto& pin = mcu.pins()[index];
            pin.subscribe([this, index](const Pin& p, Cycles at) {
                activity[index].change(p.state() == PinState::High, at);
            });
            pin.subscribeDrive([this](const Pin&, Cycles) {
                if (circuit)
                    circuit->update();
            });
            pin.subscribeSignal([this, index](const Pin& p, Cycles at) {
                recorder.record(static_cast<u16>(index), at, p.state(), p.voltage(supplyVoltage));
            });
        }
        activity = std::move(prepared);
        ++chipRevision;
        plug(mcu);
        restartCapture(mcu);
    }

    /// Starts a new capture with the present level of every pin, so that each trace has a first point.
    void restartCapture(Mcu& mcu) {
        recorder.restart();
        for (std::size_t index = 0; index < mcu.pins().size(); ++index)
            recorder.record(static_cast<u16>(index), mcu.cycles(), mcu.pins()[index].state(), mcu.pins()[index].voltage(supplyVoltage));
    }

    static bool sameName(std::string_view a, std::string_view b) {
        return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
    }

    /// The selected circuit is drawn for the MCU it names; another chip gets a bare board.
    [[nodiscard]] bool circuitFits(const Mcu& mcu) const {
        if (!description.is_object())
            return false;
        const std::string wanted = description.at("chip").at("mcu");
        return (mcuId && sameName(*mcuId, wanted)) || sameName(mcu.name(), wanted);
    }

    /// Plugs `mcu` in the selected circuit. The simulation lock is held. Returns what prevented it, if anything.
    std::optional<DescriptionError> plug(Mcu& mcu) {
        circuit.reset();
        circuitView = "null";
        ++circuitRevision;
        if (!circuitFits(mcu))
            return std::nullopt;
        auto created = circuit::Circuit::create(description);
        if (!created)
            return created.error();
        if (auto attached = (*created)->attach(mcu); !attached)
            return attached.error();
        circuit     = std::move(*created);
        auto view   = circuit->view();
        view["id"]  = circuitName;
        circuitView = view.dump(-1, ' ', true, nlohmann::json::error_handler_t::replace);
        return std::nullopt;
    }

    /// Starts a new window on every pin. The simulation lock is held, so no pin changes in between: whatever the pins
    /// did before the reset (edges, high time) cannot be mixed with the new run, however far it has gone by the next frame.
    void restartActivity(Mcu& mcu) {
        for (std::size_t index = 0; index < activity.size(); ++index) {
            auto& a       = activity[index];
            a             = Activity{};
            a.windowStart = a.highSince = mcu.cycles();
            a.high        = mcu.pins()[index].state() == PinState::High;
        }
    }

    static std::optional<std::string> catalogId(std::string_view requested) {
        for (auto id : mcu::available())
            if (std::ranges::equal(id, requested, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }))
                return std::string{id};
        return std::nullopt;
    }

    /// `next`, when given, is the circuit (description and name) to plug the new MCU in.
    void replace(std::unique_ptr<Mcu> next, std::optional<std::string> id = std::nullopt,
                 std::optional<std::pair<nlohmann::json, std::string>> nextCircuit = std::nullopt) {
        simulation.replace(std::move(next), [&](Mcu& mcu) {
            // Always acquire the simulation lock before clientsMutex, as pushState does.
            std::scoped_lock lock{clientsMutex};
            mcuId.swap(id);
            if (nextCircuit) {
                description = std::move(nextCircuit->first);
                circuitName = std::move(nextCircuit->second);
            }
            observe(mcu);  // Publishes observers only after all allocations and subscriptions succeed.
            firmwareName.clear();
            resetSerialOffsetsLocked();
        });
    }

    /// Selects a validated circuit. The MCU is replaced when the circuit names another one of the catalog.
    void useCircuit(nlohmann::json next, std::string name) {
        const auto fail = [](const DescriptionError& error) -> webfront::BridgeError {
            return {std::string{error_code::invalidCircuit}, error.path + ": " + error.message};
        };
        auto trial = circuit::Circuit::create(next);
        if (!trial)
            throw fail(trial.error());
        const std::string wanted = next.at("chip").at("mcu");
        const auto same = simulation.access([&](Mcu& mcu) { return (mcuId && sameName(*mcuId, wanted)) || sameName(mcu.name(), wanted); });
        if (!same) {
            auto device = mcu::create(wanted);
            if (!device)
                throw fail({"/chip/mcu", "Unknown MCU: " + wanted});
            if (auto fits = (*trial)->attach(*device); !fits)
                throw fail(fits.error());
            trial->reset();  // It refers to the pins of `device`, which the bridge is about to own.
            replace(std::move(device), catalogId(wanted), std::pair{std::move(next), std::move(name)});
            return;
        }
        simulation.access([&](Mcu& mcu) {
            if (circuit)
                circuit->detach();
            std::swap(description, next);
            std::swap(circuitName, name);
            if (const auto error = plug(mcu)) {
                std::swap(description, next);
                std::swap(circuitName, name);
                plug(mcu);
                throw fail(*error);
            }
            restartActivity(mcu);
        });
    }

    /// Moves terminals of a component of the plugged circuit. The simulation lock is held.
    void moveComponent(Mcu& mcu, const std::string& id, const nlohmann::json& holes) {
        if (!circuit)
            throw webfront::BridgeError{std::string{error_code::unknownComponent}, "Unknown component: " + id + ": no circuit"};
        auto moved = circuit::move(description, id, holes);
        if (!moved) {
            const auto unknown = moved.error().kind == circuit::MoveError::Kind::UnknownComponent;
            throw webfront::BridgeError{std::string{unknown ? error_code::unknownComponent : error_code::invalidPlacement},
                                        unknown ? moved.error().error.message : moved.error().error.path + ": " + moved.error().error.message};
        }
        // What the user set by hand survives the new model; the probes go, as the page forgets them with the board.
        std::vector<std::tuple<std::string, std::string, double>> kept;
        for (const auto& component : circuit->view().at("components"))
            for (const auto& [name, entry] : component.at("state").items())
                if (entry.at("interactive").get<bool>())
                    if (const auto value = circuit->state(component.at("id").get<std::string>(), name))
                        kept.emplace_back(component.at("id"), name, *value);
        circuit->detach();
        std::swap(description, *moved);
        if (const auto error = plug(mcu)) {  // The chip fitted before: only a broken invariant lands here.
            std::swap(description, *moved);
            plug(mcu);
            throw webfront::BridgeError{std::string{error_code::invalidPlacement}, error->path + ": " + error->message};
        }
        for (const auto& [component, name, value] : kept)
            (void)circuit->set(component, name, value);
    }

    void installFirmware(std::string name, const ProgramImage& image) {
        simulation.pause();
        try {
            simulation.access([&](Mcu& mcu) {
                mcu.load(image);
                if (circuit)
                    circuit->restart();
                restartActivity(mcu);
                restartCapture(mcu);
                std::scoped_lock lock{clientsMutex};
                firmwareName = std::move(name);
                resetSerialOffsetsLocked();
            });
        } catch (const std::exception& e) {
            throw webfront::BridgeError{std::string{error_code::firmwareRejected}, e.what()};
        }
    }

    /// Serves components/<type>/render.js from the component library (ADR 0005): a dynamic route, since the modules
    /// are embedded in the binary and not files of the interface directory.
    void serveRenderers() {
        if constexpr (requires { app.addRoute(std::string{}, webfront::http::RouteHandler{}); }) {
            renderers.emplace(app.addRoute("/components/", [](const webfront::http::RouteRequest& request) {
                webfront::http::RouteReply reply;
                auto                       path = request.path.substr(std::string_view{"/components/"}.size());
                const std::string_view     file = "/render.js";
                const auto script = path.ends_with(file) ? circuit::Library::builtin().renderer(path.substr(0, path.size() - file.size())) : std::string_view{};
                if (script.empty()) {
                    reply.status        = webfront::http::Response::notFound;
                    reply.contentLength = 0;
                    return reply;
                }
                reply.headers       = {{"Content-Type", "text/javascript; charset=utf-8"}, {"Cache-Control", "no-cache"}};
                reply.contentLength = script.size();
                reply.body          = [script, sent = std::size_t{0}](std::span<char> buffer) mutable {
                    const auto count = std::min(buffer.size(), script.size() - sent);
                    std::copy_n(script.data() + sent, count, buffer.data());
                    sent += count;
                    return count;
                };
                return reply;
            }));
        }
    }

    void registerFunctions() {
        app.cppFunction("sf.chip", [this] {
            return simulation.access([](Mcu& mcu) {
                return std::string{mcu.chipJson()};
            });
        });
        app.cppFunction("sf.mcus", [] {
            std::vector<std::string> names;
            for (auto name : mcu::available())
                names.emplace_back(name);
            return names;
        });
        // The three functions below reject with a webfront::BridgeError (see error_code) and return nothing on success.
        app.cppFunction("sf.selectMcu", [this](std::string name) {
            auto device = mcu::create(name);
            if (!device)
                throw webfront::BridgeError{std::string{error_code::unknownMcu}, "Unknown MCU: " + name};
            replace(std::move(device), catalogId(name));
        });
        app.cppFunction("sf.loadMcu", [this](std::string text) {
            auto device = mcu::loadJson(text);
            if (!device)
                throw webfront::BridgeError{std::string{error_code::invalidMcu}, device.error().path + ": " + device.error().message};
            replace(std::move(*device));
        });
        // The circuit the chip is plugged in, as JSON ("null" for a bare board), and the built-in ones.
        app.cppFunction("sf.circuit", [this] { return simulation.access([this](Mcu&) { return circuitView; }); });
        app.cppFunction("sf.circuits", [] { return circuit::builtinNames(); });
        app.cppFunction("sf.selectCircuit", [this](std::string name) {
            const auto names = circuit::builtinNames();
            if (std::ranges::find(names, name) == names.end())
                throw webfront::BridgeError{std::string{error_code::unknownCircuit}, "Unknown circuit: " + name};
            useCircuit(circuit::builtinDescription(name), name);
        });
        app.cppFunction("sf.loadCircuit", [this](std::string name, std::string text) {
            auto parsed = circuit::parseDescription(text);
            if (!parsed)
                throw webfront::BridgeError{std::string{error_code::invalidCircuit}, parsed.error().path + ": " + parsed.error().message};
            useCircuit(std::move(*parsed), std::move(name));
        });
        // An interaction of the user with a component: a button press (1 or 0), a potentiometer position (0 to 1).
        app.cppFunction("sf.component.set", [this](std::string id, std::string name, double value) {
            const auto result = simulation.access([&](Mcu&) -> std::expected<void, circuit::SetError> {
                if (!circuit)
                    return std::unexpected(circuit::SetError::UnknownComponent);
                return circuit->set(id, name, value);
            });
            if (result)
                return;
            const auto unknown = result.error() == circuit::SetError::UnknownComponent || result.error() == circuit::SetError::UnknownState;
            throw webfront::BridgeError{std::string{unknown ? error_code::unknownComponent : error_code::invalidValue},
                                        std::format("{}.{} cannot be set to {}{}", id, name, value,
                                                    result.error() == circuit::SetError::UnknownComponent ? ": unknown component"
                                                    : result.error() == circuit::SetError::UnknownState   ? ": unknown state"
                                                    : result.error() == circuit::SetError::NotInteractive ? ": read-only state"
                                                                                                          : ": out of bounds")};
        });
        // The user moved a component on the breadboard: `holes` is a JSON object of the terminals that moved and their
        // new holes ({"a": "h16", "b": "h23"}). The circuit is rebuilt, keeping what the user set (button, potentiometer).
        app.cppFunction("sf.component.move", [this](std::string id, std::string holes) {
            nlohmann::json parsed;
            try {
                parsed = nlohmann::json::parse(holes);
            } catch (const std::exception& e) {
                throw webfront::BridgeError{std::string{error_code::invalidPlacement}, std::string{"Invalid holes: "} + e.what()};
            }
            simulation.access([&](Mcu& mcu) { moveComponent(mcu, id, parsed); });
        });
        app.cppFunction("sf.examples", [] {
            auto list = nlohmann::json::array();
            for (const auto& example : examples)
                list.push_back({{"id", example.id}, {"title", example.title}, {"circuit", example.circuit}, {"description", example.description}});
            return list.dump();
        });
        app.cppFunction("sf.loadExample", [this](std::string id) {
            const auto example = std::ranges::find(examples, id, &Example::id);
            const auto hex     = example == std::end(examples) ? std::string_view{} : embedded::firmware(example->firmware);
            if (hex.empty())
                throw webfront::BridgeError{std::string{error_code::unknownExample}, "Unknown example: " + id};
            useCircuit(circuit::builtinDescription(example->circuit), std::string{example->circuit});
            installFirmware(std::string{example->firmware}, parseIntelHex(hex).value());
            simulation.run();
        });
        // The capture of the instruments after sample `cursor` of capture `epoch`: [epoch, first sample held, next
        // cursor, present cycle, cycles (Float64Array), pin indexes (Uint16Array), volts (Float32Array), states
        // (Uint8Array)]. A cursor older than the first sample held means that the page missed some.
        app.cppFunction("sf.signals", [this](double epoch, double cursor) {
            return simulation.access([&](Mcu& mcu) {
                std::vector<double>        at;
                std::vector<std::uint16_t> channel;
                std::vector<float>         volts;
                std::vector<std::uint8_t>  state;
                const auto current = static_cast<double>(recorder.epoch()) == epoch && cursor >= 0;
                const auto next    = recorder.read(current ? static_cast<u64>(cursor) : 0, maxSignalsPerCall, [&](const instruments::Sample& sample) {
                    at.push_back(static_cast<double>(sample.at));
                    channel.push_back(sample.channel);
                    volts.push_back(sample.volts);
                    state.push_back(static_cast<std::uint8_t>(sample.state));
                });
                return std::tuple{static_cast<double>(recorder.epoch()), static_cast<double>(recorder.first()), static_cast<double>(next),
                                  static_cast<double>(mcu.cycles()), std::move(at), std::move(channel), std::move(volts), std::move(state)};
            });
        });
        app.cppFunction("sf.run", [this] { simulation.run(); });
        app.cppFunction("sf.pause", [this] { simulation.pause(); });
        app.cppFunction("sf.step", [this] { simulation.step(); });
        app.cppFunction("sf.reset", [this] {
            simulation.reset([this](Mcu& mcu) {
                if (circuit)
                    circuit->restart();
                restartActivity(mcu);
                restartCapture(mcu);
            });
            resetSerialOffsets();
        });
        app.cppFunction("sf.setSpeed", [this](double factor) { simulation.setSpeed(factor); });
        app.cppFunction("sf.serialSend", [this](std::string text) {
            simulation.access([&](Mcu& mcu) {
                if (auto* port = mcu.serial(0))
                    port->send(text);
            });
        });
        // level: 0 low, 1 high, anything else releases the pin. In a circuit this is a probe on the net of the pin.
        app.cppFunction("sf.setPin", [this](std::string name, int level) {
            return simulation.access([&](Mcu& mcu) {
                auto* pin = mcu.pin(name);
                if (!pin)
                    return false;
                const auto forced = level == 0 || level == 1 ? std::optional<bool>{level == 1} : std::nullopt;
                if (!circuit || !circuit->force(name, forced ? std::optional{*forced ? supplyVoltage : 0.0} : std::nullopt))
                    pin->setExternal(forced);
                return true;
            });
        });
        // In a circuit, the voltage is applied by turning the potentiometer that feeds the pin, when there is one.
        app.cppFunction("sf.setVoltage", [this](std::string name, double volts) {
            return simulation.access([&](Mcu& mcu) {
                auto* pin = mcu.pin(name);
                if (pin && (!circuit || !circuit->drive(name, volts)))
                    pin->setVoltage(volts, supplyVoltage);
                return pin != nullptr;
            });
        });
        // Firmware sent by the browser (Intel HEX or ELF content). Rejects with a BridgeError when it cannot be loaded.
        app.cppFunction("sf.loadFirmware", [this](std::string name, std::vector<std::uint8_t> bytes) {
            const auto isElf = bytes.size() >= 4 && bytes[0] == 0x7F && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F';
            auto image = isElf ? parseElf(bytes) : parseIntelHex(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            if (!image)
                throw webfront::BridgeError{std::string{error_code::invalidFirmware},
                                            image.error().line ? std::format("{} (line {})", image.error().message, image.error().line) : image.error().message};
            installFirmware(std::move(name), *image);
        });
        app.cppFunction("sf.addBreakpoint", [this](double address) {
            simulation.access([&](Mcu& mcu) { mcu.debugger().addBreakpoint(static_cast<Address>(address)); });
        });
        app.cppFunction("sf.clearBreakpoints", [this] { simulation.access([](Mcu& mcu) { mcu.debugger().clearBreakpoints(); }); });
    }

    void resetSerialOffsets() {
        std::scoped_lock lock{clientsMutex};
        resetSerialOffsetsLocked();
    }

    // clientsMutex must be held. The epoch prevents an in-flight frame from undoing a reset at offset zero.
    void resetSerialOffsetsLocked() {
        ++serialEpoch;
        for (auto& [id, client] : clients)
            client.serialOffset = 0;
    }

    void pushState() {
        // Snapshot under simulation -> clients locks. Send only after releasing both locks.
        std::vector<std::pair<webfront::WebLinkId, Client>> targets;
        nlohmann::json state;
        std::string serialOutput;
        std::uint64_t epoch{};
        simulation.snapshot([&](Mcu& mcu, std::optional<StopReason> reason) {
            std::scoped_lock lock{clientsMutex};
            if (clients.empty())
                return;
            for (const auto& [id, client] : clients)
                targets.emplace_back(id, client);
            epoch = serialEpoch;
            const auto now = mcu.cycles();
            state = {
                {"mcu", mcu.name()},
                {"mcuId", mcuId ? nlohmann::json(*mcuId) : nlohmann::json(nullptr)},
                {"arch", mcu.architecture()},
                {"clockHz", mcu.clockHz()},
                {"cycles", now},
                {"pc", mcu.programCounter()},
                {"running", simulation.isRunning()},
                {"speed", simulation.speedFactor()},
                {"symbol", mcu.symbolAt(mcu.programCounter())},
                {"firmware", firmwareName},
                {"chipRevision", chipRevision},
                {"circuitRevision", circuitRevision},
                {"components", circuit ? circuit->frame() : nlohmann::json::object()},
                {"shorts", circuit ? nlohmann::json(circuit->shorts()) : nlohmann::json::array()},
                {"frame", ++frameCounter}  // Consecutive numbers received by a client prove that no frame was skipped.
            };
            state["stop"] = reason ? nlohmann::json(toString(*reason)) : nlohmann::json(nullptr);
            state["pins"] = nlohmann::json::array();
            for (std::size_t index = 0; index < mcu.pins().size(); ++index) {
                const auto& pin = mcu.pins()[index];
                auto& a = activity[index];
                const auto sample = a.takeFrame(now);
                state["pins"].push_back({
                    {"name", pin.name()},
                    {"label", pin.label()},
                    {"state", static_cast<int>(pin.state())},
                    {"output", pin.isOutput()},
                    {"pullUp", pin.hasPullUp()},
                    {"duty", std::round(sample.duty * 1000.0) / 1000.0},
                    {"volts", std::round(pin.voltage(supplyVoltage) * 1000.0) / 1000.0},
                    {"edges", sample.edges}
                });
            }
            state["registers"] = nlohmann::json::array();
            for (const auto& r : mcu.registers())
                state["registers"].push_back(nlohmann::json::array({r.name, r.value, r.bits}));
            if (auto* port = mcu.serial(0))
                serialOutput = port->output();
        });

        for (auto& [id, client] : targets) {
            if (client.ui.pendingWrites() > 4)  // A slow browser skips frames instead of accumulating them.
                continue;
            auto offset = client.serialOffset > serialOutput.size() ? 0 : client.serialOffset;
            // A frame carries at most maxSerialPerFrame bytes of serial output, so that a large burst does not delay the pins.
            const auto chunk = std::string_view{serialOutput}.substr(offset, maxSerialPerFrame);
            // Serial is a byte stream: map each byte to U+00XX, preserving the legacy browser contract.
            // This also preserves bytes when a UTF-8 sequence straddles two chunks.
            std::string text;
            text.reserve(chunk.size() * 2);
            for (const unsigned char byte : chunk) {
                if (byte >= 0x80) {
                    text += static_cast<char>(0xC0 | (byte >> 6));
                    text += static_cast<char>(0x80 | (byte & 0x3F));
                } else
                    text += static_cast<char>(byte);
            }
            state["serial"] = std::move(text);
            try {
                // Names, ELF symbols and paths may contain invalid UTF-8; serial has already been transcoded.
                const auto json = state.dump(-1, ' ', true, nlohmann::json::error_handler_t::replace);
                client.ui.jsFunction("siliconForge.onState")(json);
            } catch (const std::exception&) {
                continue;  // The browser disconnected meanwhile; subscribeUIClosed removes it.
            }
            std::scoped_lock lock{clientsMutex};
            if (auto found = clients.find(id); found != clients.end() && serialEpoch == epoch && found->second.serialOffset == client.serialOffset)
                found->second.serialOffset = offset + chunk.size();  // Unless a reset or a new firmware restarted it.
        }
    }

    App&                     app;
    Simulation&              simulation;
    std::string              firmwareName;
    std::optional<std::string> mcuId;  // Catalog ID, or null for a directly loaded description; simulation lock.
    std::vector<Activity>    activity;
    // The circuit and the capture are only touched with the simulation lock held.
    nlohmann::json                     description;  // Selected circuit, null for none.
    std::string                        circuitName;
    std::unique_ptr<circuit::Circuit>  circuit;      // The selected circuit when the present MCU is the one it names.
    std::string                        circuitView{"null"};
    unsigned                           circuitRevision{0};
    instruments::SignalRecorder        recorder;
    std::optional<webfront::http::RouteTable::Registration> renderers;
    unsigned                 chipRevision{0};
    std::uint64_t            serialEpoch{0};  // Protected by clientsMutex.
    std::uint64_t            frameCounter{0};  // Only touched by the pushing thread.
    std::mutex               clientsMutex;
    std::map<webfront::WebLinkId, Client> clients;
    webfront::UISubscription closed;
    std::jthread             pusher;
};

}  // namespace siliconforge::app
