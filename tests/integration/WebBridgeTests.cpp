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

TEST_CASE("Web state replaces invalid UTF-8 in names, ELF symbols, paths and serial", "[app][bridge]") {
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
    CHECK(nlohmann::json::parse(values.front())["serial"] == "\xef\xbf\xbd");
}

TEST_CASE("MCU replacement keeps frame metadata coherent and discards stale serial acknowledgements", "[app][bridge]") {
    auto device = mcu::create("atmega328p");
    device->serial()->transmit('o', 0);
    device->serial()->transmit('l', 0);
    device->serial()->transmit('d', 0);
    app::Simulation simulation{std::move(device)};
    TestApp web;
    auto frames = std::make_shared<Frames>();
    bool replaced = false;
    app::WebBridge bridge{web, simulation, "old.hex"};
    frames->beforeSend = [&] {
        if (replaced)
            return;
        replaced = true;
        // Replace while the old frame is in flight, with its previous offset equal to zero.
        web.select("atmega328p");
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
    CHECK(old["serial"] == "old");
    CHECK(next["chipRevision"].get<unsigned>() == old["chipRevision"].get<unsigned>() + 1);
    CHECK(next["firmware"] == "");
    CHECK(next["serial"] == "replacement");
}
