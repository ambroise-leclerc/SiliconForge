# SiliconForge

[Français](README.md) · English (UK)

**Emulate a microcontroller, observe your circuit and test your firmware without hardware.**

SiliconForge is a native application that runs the `.hex` and `.elf` binaries produced by your toolchain. It brings
the circuit, pinout, serial monitor and CPU registers together in one window. Emulation follows the
microcontroller’s clock cycles, with simulated peripherals and I/O, so you can iterate on firmware and check its
behaviour deterministically.

The first target is the **ATmega328P at 16 MHz**, the microcontroller used in the Arduino Uno. The application opens
a native window on macOS, Linux and Windows through [WebFront](https://github.com/ambroise-leclerc/WebFront) and
the available system engine. It can also run headlessly for tests and continuous integration.

## Observe and control firmware

Animated capture of the native **macOS** window with the light grey theme: `blink.hex` makes the D13 LED blink
while the cycle counter and registers update.

![SiliconForge’s native macOS window: breadboard circuit, serial monitor and CPU registers](docs/images/siliconforge.gif)

The **Breadboard** view connects LEDs, a push button and a potentiometer to the chip: the LED on D13 follows the
firmware, the one on D6 reflects its PWM duty cycle, and the controls drive D2 and A0. You can run, pause, reset or
step through one instruction, adjust the speed and load another firmware binary.

The **Pinout** view shows physical pin numbers, ports, functions and Arduino names, with live levels shown in the
animated capture below. Click an input to drive or release it. Not every function labelled on the pinout is emulated; the table below lists the
available peripherals.

![SiliconForge’s native macOS window: interactive ATmega328P pinout and CPU state](docs/images/pinout.gif)

## Current status

| Part | Support |
| --- | --- |
| AVRe+ core | ATmega328P instruction set and cycle counts; interrupts with priorities, 4-cycle response and SEI/RETI delay; sleep, BREAK and SPM self-programming |
| Memories | 32 KiB flash, 2 KiB SRAM, 1 KiB EEPROM with write timing |
| GPIO | Ports B, C and D: DDR/PORT/PIN, PIN toggle, pull-ups and `MCUCR.PUD` |
| Timer/Counter0 | Normal, CTC, Fast PWM and Phase Correct PWM modes; OC0A/OC0B outputs and interrupts |
| USART0 | Asynchronous mode at the configured baud rate, two-level receive FIFO, overrun and interrupts |
| ADC | Single conversions on A0–A5 and the internal reference, with conversion timing |
| MCU descriptions | Versioned JSON format, schema and reference validation, C++ loading and an embedded ATmega328P description |
| Tests | Instructions, peripherals, description loading and real firmware through the test bench; Jasmine interaction scenarios available |
| To implement | Timer1/2, SPI, TWI, watchdog, analogue comparator and external interrupts; their registers remain plain storage |

### JSON microcontroller descriptions

ATmega328P parameters now come from [`mcus/atmega328p.json`](mcus/atmega328p.json): memories, registers and reset
values, vectors, instruction timings, peripherals, package and pinout. The description and its schema are embedded
in the binary. Validation combines JSON Schema Draft 7 with C++ consistency checks, reporting errors by JSON Pointer.

The C++ API loads descriptions from a file (`mcu::load`), text (`mcu::loadJson`) or a directory added to the catalogue
(`mcu::addPath`). Available factories assemble AVR GPIO, 8-bit timers, USART, ADC and EEPROM. JSON configures
behaviour already implemented in the emulator; a new peripheral type or instruction still requires C++ code.

The decoder now shares a compact table of 65,536 16-bit indices across cores. Each chip configures its handlers
and timings at creation, with no JSON lookup in the emulation loop. See [MCU descriptions](docs/MCU-DESCRIPTIONS.md)
for the format, APIs and limitations.

### Measured performance

For `blink`, the latest documented session reports a median of **156.4 MHz of simulated clock cycles**, or about
**9.8 times real time** for a 16 MHz ATmega328P (GCC 16.1, Release, core with 512 KiB L2 cache). Throughput depends
on the host and firmware. The ratio of medians shows a 4.02% reduction against the initial engine in that session;
the ±3% target remains unverified. The [method and results](docs/MCU-DESCRIPTIONS.md#mesures-de-la-table-compacte)
distinguish measurements from hypotheses about their causes.

## Quick start

Requirements: **CMake 3.31+** and a compiler with a **C++26** standard library. Linux CI uses GCC 16; the macOS
screenshots above were captured from an Apple Clang 21 build. Dependencies (WebFront, Catch2, nlohmann/json and
the JSON Schema validator) are fetched by CPM.

On macOS, the native window uses WKWebView. On Linux, install WebKitGTK 6.0 before configuring
(`sudo apt install libwebkitgtk-6.0-dev` on Debian/Ubuntu, `webkitgtk6.0-devel` on Fedora). Without a native engine
available at build time, the application opens the system browser.

From the repository root:

```bash
cmake -S . -B build                 # Release by default
cmake --build build -j
./build/apps/siliconforge/siliconforge firmware/prebuilt/blink.hex
```

The [prebuilt firmware](firmware/README.md) lets you try blinking, serial echo, Timer0 interrupts and PWM without
installing an AVR toolchain.

For a headless run with serial output on standard output:

```bash
./build/apps/siliconforge/siliconforge --headless --speed max --duration 5 firmware/prebuilt/uart_echo.hex
```

`--browser` opens the system browser; `--address 0.0.0.0 --no-browser` allows access from another machine on port
9002. `--mcu atmega328p` selects the target; `--help` lists options and models in the catalogue.

## Test firmware

The [C++ test bench](include/siliconforge/testing/Bench.hpp) loads a binary, advances simulated time and observes
pins or serial traffic. This Catch2 example uses the demonstration firmware from the repository root:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

using namespace std::chrono_literals;

TEST_CASE("the firmware replies in upper case") {
    siliconforge::testing::Bench bench{siliconforge::mcu::create("atmega328p")};
    bench.load("firmware/prebuilt/uart_echo.hex");
    REQUIRE(bench.waitForSerial("ready\r\n", 100ms));
    bench.serial().send("hello\r");
    REQUIRE(bench.waitForSerial("HELLO\r\n", 100ms));
}
```

Run the repository’s automated tests with:

```bash
ctest --test-dir build --output-on-failure
```

The application also exposes `window.siliconForge` to drive controls, pins and the serial monitor in interaction
scenarios. The [Jasmine scenarios and execution instructions](tests/ui/README.md) are available; CTest integration
through `wf test` remains to be implemented.

## Project structure

```text
include/siliconforge/
├── core/       MCU interface, scheduler, pins, serial ports, debugger, HEX/ELF loaders
├── arch/avr/   AVR core and reusable peripherals
├── mcu/        Descriptions, validation, catalogue and ATmega328P façade
└── testing/    Test bench
src/            C++ implementations
mcus/           JSON microcontroller descriptions
schemas/        MCU format schema
apps/siliconforge/   Application, simulation and control bridge
ui/             Circuit, pinout, pin and CPU views
firmware/       Test firmware and prebuilt binaries
```

The shared foundation is independent of any instruction set. A chip reusing existing AVR behaviour can be
described in JSON; another architecture requires a core and peripherals under `arch/`. See the
[architecture](docs/ARCHITECTURE.md) and [architecture decisions](docs/adr).

## Next steps

| Direction | Status and reference |
| --- | --- |
| End-to-end tested demonstration | Circuit and controls available; full automation remains — [ADR 0003](docs/adr/0003-demo-bout-en-bout.md) |
| MCU descriptions | Versioned format and ATmega328P integrated; performance qualification continues — [ADR 0004](docs/adr/0004-description-mcu-json.md) |
| JSON components and circuits | Demonstration circuit included in the description; general component and circuit model remains — [ADR 0005](docs/adr/0005-composants-et-circuits.md) |
| Core qualification | Further decoding coverage, differential tests with simavr and cycle verification — [ADR 0006](docs/adr/0006-qualification-coeur.md) |

Also planned: Timer1/2, external and pin-change interrupts, SPI, TWI, watchdog; simulated displays; GDB remote
protocol, disassembly and a call stack from ELF symbols; further targets including ATmega2560, ARM Cortex-M0+/M4
and RISC-V.

## Licence

SiliconForge is dual-licensed under **Apache-2.0 OR CECILL-B**, at your option. [LICENSE](LICENSE) contains the full
Apache 2.0 and CeCILL-B version 1 texts, including the French and English versions of CeCILL-B. Third-party
components retain their own licences.
