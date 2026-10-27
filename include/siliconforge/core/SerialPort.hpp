#pragma once

#include <siliconforge/core/Types.hpp>

#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace siliconforge {

/// Host side of a simulated serial interface (USART, UART...).
///
/// The peripheral pushes every transmitted byte here; the host queues bytes that the peripheral receives at the
/// configured baud rate. The transmitted bytes are kept so that tests and the UI can inspect the whole output.
class SerialPort {
public:
    using Listener = std::function<void(u8 byte, Cycles at)>;

    explicit SerialPort(std::string name) : portName(std::move(name)) {}

    [[nodiscard]] const std::string& name() const noexcept { return portName; }

    // Host API ----------------------------------------------------------------------------------------------------
    void send(std::string_view text) {
        rxQueue.insert(rxQueue.end(), text.begin(), text.end());
        if (inputListener)
            inputListener();
    }
    void send(u8 byte) { send(std::string_view{reinterpret_cast<const char*>(&byte), 1}); }

    /// Everything transmitted by the MCU since reset (or since the last clearOutput()).
    [[nodiscard]] const std::string& output() const noexcept { return transmitted; }
    void clearOutput() noexcept { transmitted.clear(); }

    void onTransmit(Listener listener) { listeners.push_back(std::move(listener)); }

    // Peripheral API ----------------------------------------------------------------------------------------------
    /// Called whenever the host queues input, so that an idle receiver can start receiving.
    void setInputListener(std::function<void()> listener) { inputListener = std::move(listener); }
    [[nodiscard]] bool hasPendingInput() const noexcept { return !rxQueue.empty(); }
    u8 takeInput() {
        const auto byte = static_cast<u8>(rxQueue.front());
        rxQueue.pop_front();
        return byte;
    }

    void transmit(u8 byte, Cycles at) {
        transmitted.push_back(static_cast<char>(byte));
        for (auto& listener : listeners)
            listener(byte, at);
    }

    void reset() {
        rxQueue.clear();
        transmitted.clear();
    }

private:
    std::string           portName;
    std::deque<char>      rxQueue;
    std::string           transmitted;
    std::vector<Listener> listeners;
    std::function<void()> inputListener;
};

}  // namespace siliconforge
