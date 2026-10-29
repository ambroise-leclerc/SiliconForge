#include "../../apps/siliconforge/WebBridge.hpp"
#include <catch2/catch_test_macros.hpp>
#include <siliconforge/arch/avr/Device.hpp>
#include <siliconforge/mcu/Description.hpp>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <memory>
#include <type_traits>

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
    std::function<std::string(std::string)> select;
    template <typename F>
    void cppFunction(std::string_view name, F f) {
        if constexpr (std::is_invocable_r_v<std::string, F, std::string>) {
            if (name == "sf.selectMcu")
                select = std::move(f);
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

TEST_CASE("Web state replaces invalid UTF-8 in names, ELF symbols and paths", "[app][bridge]") {
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
    REQUIRE(values.size() >= 2);  // The pusher survived and sent a subsequent frame.
    for (const auto& text : values) {
        CHECK(std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 128; }));
        const auto state = nlohmann::json::parse(text);
        CHECK(state["mcu"] == "chip\xef\xbf\xbd");
        CHECK(state["symbol"] == "symbol\xef\xbf\xbd");
        CHECK(state["firmware"] == "firmware\xef\xbf\xbd.hex");
    }
    CHECK(nlohmann::json::parse(values.front())["serial"] == "\xc3\xbf");
}

TEST_CASE("MCU replacement keeps frame metadata coherent and discards stale serial acknowledgements", "[app][bridge]") {
    auto device = mcu::create("atmega328p");
    device->serial()->transmit('o', 0);
    device->serial()->transmit('l', 0);
    device->serial()->transmit('d', 0);
    app::Simulation simulation{std::move(device)};
    simulation.step();
    const auto previousStop = simulation.stopReason();
    REQUIRE(previousStop);
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
    REQUIRE(values.size() >= 2);
    const auto old = nlohmann::json::parse(values[0]);
    const auto next = nlohmann::json::parse(values[1]);
    CHECK(old["firmware"] == "old.hex");
    CHECK(old["mcuId"] == "atmega328p");
    CHECK(old["stop"] == app::toString(*previousStop));
    CHECK(old["serial"] == "old");
    CHECK(next["chipRevision"].get<unsigned>() == old["chipRevision"].get<unsigned>() + 1);
    CHECK(next["firmware"] == "");
    CHECK(next["mcuId"] == "atmega328p");
    CHECK(next["stop"].is_null());
    CHECK(next["serial"] == "replacement");
}

TEST_CASE("Serial preserves every byte and sequences crossing a frame boundary", "[app][bridge]") {
    auto device = mcu::create("atmega328p");
    // Split C3 A9 across the 16 KiB boundary. Neither byte may be decoded or replaced.
    std::string bytes(16 * 1024 - 1, 'x');
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
    REQUIRE(values.size() >= 2);
    const auto first = nlohmann::json::parse(values[0])["serial"].get<std::string>();
    const auto second = nlohmann::json::parse(values[1])["serial"].get<std::string>();
    CHECK(first == std::string(16 * 1024 - 1, 'x') + "\xc3\x83");  // U+00C3
    CHECK(second == "\xc2\xa9" + nlohmann::json::parse(escaped).get<std::string>());  // U+00A9 then all 256 bytes.
    CHECK(values[1].find("\\u00b0") != std::string::npos);  // Latin-1 degree sign is retained.
}

TEST_CASE("A failed MCU initializer preserves the current simulation", "[app][simulation]") {
    auto device = mcu::create("atmega328p");
    const auto* original = device.get();
    app::Simulation simulation{std::move(device)};
    simulation.step();
    const auto previousStop = simulation.stopReason();
    const auto previousCycles = simulation.access([](Mcu& mcu) { return mcu.cycles(); });
    CHECK_THROWS_AS(simulation.replace(mcu::create("atmega328p"), [](Mcu&) {
        throw std::runtime_error("initializer failed");
    }), std::runtime_error);
    simulation.snapshot([&](Mcu& mcu, std::optional<StopReason> reason) {
        CHECK(&mcu == original);
        CHECK(mcu.cycles() == previousCycles);
        CHECK(reason == previousStop);
    });
}
