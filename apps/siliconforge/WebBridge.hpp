#pragma once

#include "Simulation.hpp"

#include <nlohmann/json.hpp>
#include <siliconforge/core/loader/ProgramImage.hpp>
#include <siliconforge/mcu/Registry.hpp>

#include <webfront/WebFront.hpp>

#include <chrono>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace siliconforge::app {

/// Appends `text` as a JSON string literal.
inline void appendJsonString(std::string& out, std::string_view text) {
    out += '"';
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F)
                    out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                else
                    out += c;
        }
    }
    out += '"';
}

/// Connects a Simulation to the browsers of a WebFront application.
///
/// The browser calls `sf.*` C++ functions to control the MCU; the bridge pushes the MCU state to every connected
/// browser about 30 times per second by calling `siliconForge.onState(json)`. Each pin carries the fraction of the
/// last frame it spent high, so that a PWM-driven LED is displayed with its average brightness.
template <typename App>
class WebBridge {
public:
    using UI = typename App::UI;

    WebBridge(App& app, Simulation& simulation, std::string firmware) : app(app), simulation(simulation), firmwareName(std::move(firmware)) {
        simulation.access([this](Mcu& mcu) {
            observe(mcu);
        });
        registerFunctions();
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

private:
    /// High time of one pin over the current frame.
    struct Activity {
        Cycles windowStart{0};
        Cycles highSince{0};
        Cycles highTotal{0};
        u32    edges{0};  ///< Changes during the frame: two or more mean a PWM or a fast signal.
        bool   high{false};

        void change(bool nowHigh, Cycles at) {
            ++edges;
            if (high)
                highTotal += at - std::max(highSince, windowStart);
            high      = nowHigh;
            highSince = at;
        }

        double takeDuty(Cycles now) {
            if (now < windowStart) {  // The MCU was reset.
                windowStart = highSince = now;
                highTotal   = 0;
            }
            if (now == windowStart)
                return high ? 1.0 : 0.0;
            const auto total = highTotal + (high ? now - std::max(highSince, windowStart) : 0);
            const auto duty  = static_cast<double>(total) / static_cast<double>(now - windowStart);
            windowStart      = now;
            highTotal        = 0;
            edges            = 0;
            return duty;
        }
    };

    struct Client {
        UI          ui;
        std::size_t serialOffset;
    };

    void observe(Mcu& mcu) {
        activity.assign(mcu.pins().size(), Activity{});
        for (std::size_t index = 0; index < mcu.pins().size(); ++index)
            mcu.pins()[index].subscribe([this, index](const Pin& p, Cycles at) {
                activity[index].change(p.state() == PinState::High, at);
            });
        ++chipRevision;
    }
    void replace(std::unique_ptr<Mcu> next) {
        simulation.replace(std::move(next), [this](Mcu& mcu) {
            observe(mcu);
        });
        std::scoped_lock lock{clientsMutex};
        firmwareName.clear();
        for (auto& [id, client] : clients)
            client.serialOffset = 0;
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
            return nlohmann::json(names).dump();
        });
        app.cppFunction("sf.selectMcu", [this](std::string name) -> std::string {
            auto device = mcu::create(name);
            if (!device)
                return "Unknown MCU: " + name;
            replace(std::move(device));
            return {};
        });
        app.cppFunction("sf.loadMcu", [this](std::string text) -> std::string {
            auto device = mcu::loadJson(text);
            if (!device)
                return device.error().path + ": " + device.error().message;
            replace(std::move(*device));
            return {};
        });
        app.cppFunction("sf.run", [this] { simulation.run(); });
        app.cppFunction("sf.pause", [this] { simulation.pause(); });
        app.cppFunction("sf.step", [this] { simulation.step(); });
        app.cppFunction("sf.reset", [this] {
            simulation.reset();
            resetSerialOffsets();
        });
        app.cppFunction("sf.setSpeed", [this](double factor) { simulation.setSpeed(factor); });
        app.cppFunction("sf.serialSend", [this](std::string text) {
            simulation.access([&](Mcu& mcu) {
                if (auto* port = mcu.serial(0))
                    port->send(text);
            });
        });
        // level: 0 low, 1 high, anything else releases the pin.
        app.cppFunction("sf.setPin", [this](std::string name, int level) {
            return simulation.access([&](Mcu& mcu) {
                auto* pin = mcu.pin(name);
                if (!pin)
                    return false;
                pin->setExternal(level == 0 || level == 1 ? std::optional<bool>{level == 1} : std::nullopt);
                return true;
            });
        });
        app.cppFunction("sf.setVoltage", [this](std::string name, double volts) {
            return simulation.access([&](Mcu& mcu) {
                auto* pin = mcu.pin(name);
                if (pin)
                    pin->setVoltage(volts, 5.0);
                return pin != nullptr;
            });
        });
        // Firmware sent by the browser (Intel HEX or ELF content). Returns an error message, empty on success.
        app.cppFunction("sf.loadFirmware", [this](std::string name, std::vector<std::uint8_t> bytes) -> std::string {
            const auto isElf = bytes.size() >= 4 && bytes[0] == 0x7F && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F';
            auto image = isElf ? parseElf(bytes) : parseIntelHex(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            if (!image)
                return image.error().line ? std::format("{} (line {})", image.error().message, image.error().line) : image.error().message;
            simulation.pause();
            try {
                simulation.access([&](Mcu& mcu) { mcu.load(*image); });
            } catch (const std::exception& e) {
                return e.what();
            }
            {
                std::scoped_lock lock{clientsMutex};
                firmwareName = std::move(name);
            }
            resetSerialOffsets();
            return {};
        });
        app.cppFunction("sf.addBreakpoint", [this](double address) {
            simulation.access([&](Mcu& mcu) { mcu.debugger().addBreakpoint(static_cast<Address>(address)); });
        });
        app.cppFunction("sf.clearBreakpoints", [this] { simulation.access([](Mcu& mcu) { mcu.debugger().clearBreakpoints(); }); });
    }

    void resetSerialOffsets() {
        std::scoped_lock lock{clientsMutex};
        for (auto& [id, client] : clients)
            client.serialOffset = 0;
    }

    void pushState() {
        // Never call WebFront while holding a lock: its server thread runs our callbacks, which take these locks.
        std::vector<std::pair<webfront::WebLinkId, Client>> targets;
        std::string                                         firmware;
        {
            std::scoped_lock lock{clientsMutex};
            if (clients.empty())
                return;
            for (const auto& [id, client] : clients)
                targets.emplace_back(id, client);
            firmware = firmwareName;
        }

        const auto  reason  = simulation.stopReason();
        std::string common;
        std::string serialOutput;
        simulation.access([&](Mcu& mcu) {
            const auto now = mcu.cycles();
            nlohmann::json state{
                {         "mcu", mcu.name()},
                {        "arch",
                                         mcu.architecture()},
                {     "clockHz", mcu.clockHz()},
                {      "cycles", now},
                {          "pc", mcu.programCounter()},
                {     "running", simulation.isRunning()},
                {       "speed", simulation.speedFactor()},
                {      "symbol", mcu.symbolAt(mcu.programCounter())},
                {    "firmware", firmware},
                {"chipRevision",                       chipRevision}
            };
            state["stop"] = reason ? nlohmann::json(toString(*reason)) : nlohmann::json(nullptr);
            state["pins"] = nlohmann::json::array();
            for (std::size_t index = 0; index < mcu.pins().size(); ++index) {
                const auto& pin = mcu.pins()[index];
                auto&       a     = activity[index];
                const auto edges       = a.edges;
                state["pins"].push_back({
                    {  "name",
                                      pin.name()},
                    { "label", pin.label()},
                    { "state", static_cast<int>(pin.state())},
                    {"output", pin.isOutput()},
                    {"pullUp", pin.hasPullUp()},
                    {  "duty",               a.takeDuty(now)},
                    { "edges", edges}
                });
            }
            state["registers"] = nlohmann::json::array();
            for (const auto& r : mcu.registers())
                state["registers"].push_back(nlohmann::json::array({r.name, r.value, r.bits}));
            common = state.dump();
            common = common.substr(1, common.size() - 2);
            if (auto* port = mcu.serial(0))
                serialOutput = port->output();
        });

        for (auto& [id, client] : targets) {
            if (client.ui.pendingWrites() > 4)  // A slow browser skips frames instead of accumulating them.
                continue;
            auto offset = client.serialOffset > serialOutput.size() ? 0 : client.serialOffset;
            // Strings crossing the bridge are limited to 64 KiB: a large burst is sent over several frames.
            const auto chunk = std::string_view{serialOutput}.substr(offset, 16 * 1024);
            auto       json  = "{" + common + R"(,"serial":)";
            appendJsonString(json, chunk);
            json += "}";
            try {
                client.ui.jsFunction("siliconForge.onState")(json);
            } catch (const std::exception&) {
                continue;  // The browser disconnected meanwhile; subscribeUIClosed removes it.
            }
            std::scoped_lock lock{clientsMutex};
            if (auto found = clients.find(id); found != clients.end() && found->second.serialOffset == client.serialOffset)
                found->second.serialOffset = offset + chunk.size();  // Unless a reset or a new firmware restarted it.
        }
    }

    App&                     app;
    Simulation&              simulation;
    std::string              firmwareName;
    std::vector<Activity>    activity;
    unsigned                              chipRevision{0};
    std::mutex               clientsMutex;
    std::map<webfront::WebLinkId, Client> clients;
    webfront::UISubscription closed;
    std::jthread             pusher;
};

}  // namespace siliconforge::app
