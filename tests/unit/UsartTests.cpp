#include "../support/AvrFixture.hpp"

#include <webfront/testing/Bdd.hpp>

using namespace webfront::bdd;

using namespace siliconforge;
using siliconforge::test::AvrAssembler;
using siliconforge::test::AvrFixture;

namespace {
constexpr Address ucsr0a = 0xC0, ucsr0b = 0xC1, ubrr0l = 0xC4, udr0 = 0xC6;
constexpr u8      rxc = 0x80, txc = 0x40, udre = 0x20, dor = 0x08, rxen = 0x10, txen = 0x08;

// 9600 bauds at 16 MHz: UBRR = 103, 16 * 104 = 1664 cycles per bit, 10 bits per 8N1 frame.
constexpr Cycles frame = 16640;

struct Usart9600 : AvrFixture {
    avr::DataSpace& data{mcu.dataSpace()};
    SerialPort&     serial{*mcu.serial(0)};

    explicit Usart9600(u8 enable) : AvrFixture(AvrAssembler{}.org(16 * 1024)) {  // NOPs over the whole flash: one cycle per instruction
        data.write(ubrr0l, 103);
        data.write(ucsr0b, enable);
    }
};
}  // namespace

const Scenario usart0TransmitsAtTheConfiguredBaudRate{"USART0 transmits at the configured baud rate", {"avr", "usart"}, [] {
    Usart9600 usart{txen};
    check(usart.mcu.usart0().baudRate(usart.mcu.clockHz()) == 9615);
    check(usart.mcu.pin("PD1")->state() == PinState::High);  // TXD idles high

    when("two bytes are written back to back", [&] {
        usart.data.write(udr0, 'O');
        then("the first moves to the shift register at once and UDRE0 is set again", [&] { check((usart.data.read(ucsr0a) & udre) != 0); });
        usart.data.write(udr0, 'K');
        check((usart.data.read(ucsr0a) & udre) == 0);

        andThen("each byte reaches the host after one full frame", [&] {
            usart.mcu.run(frame - 1);
            check(usart.serial.output().empty());
            usart.mcu.run(1);
            check(usart.serial.output() == "O");
            check((usart.data.read(ucsr0a) & udre) != 0);
            usart.mcu.run(frame);
            check(usart.serial.output() == "OK");
            check((usart.data.read(ucsr0a) & txc) != 0);
        });
    });
}};

const Scenario usart0ReceivesBytesSentByTheHost{"USART0 receives bytes sent by the host", {"avr", "usart"}, [] {
    Usart9600 usart{rxen};
    usart.serial.send("hi");
    when("one frame elapses", [&] {
        usart.mcu.run(frame);
        then("the first byte is available in UDR0", [&] {
            check((usart.data.read(ucsr0a) & rxc) != 0);
            check(usart.data.read(udr0) == 'h');
            check((usart.data.read(ucsr0a) & rxc) == 0);
        });
    });
    when("firmware does not read three bytes in time", [&] {
        usart.serial.send("!");
        usart.mcu.run(3 * frame);
        then("the two-level FIFO keeps the first two bytes and DOR0 reports the lost one", [&] {
            check((usart.data.read(ucsr0a) & dor) != 0);
            check(usart.data.read(udr0) == 'h');
            check(usart.data.read(udr0) == 'i');
            check((usart.data.read(ucsr0a) & rxc) == 0);
        });
    });
}};
