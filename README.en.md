# SiliconForge

[Français](README.md) · English (UK)

**Test your embedded code straight away, without flashing a board.**

Load the `.hex` or `.elf` binary produced by your toolchain and run it on a simulated microcontroller. With
SiliconForge, you can check an LED blink, exchange serial messages, try an analogue input or inspect registers
without connecting any hardware. Pause execution, step through instructions and repeat as often as you need.

SiliconForge is a native, cross-platform application written in C++, for **macOS, Linux and Windows**.
The first supported microcontroller is the **16 MHz ATmega328P**, used in the Arduino Uno.

## Your code, a circuit, a visible result

The breadboard brings together a microcontroller and simulated components. Start the application and pick
**Open an example…**: the `demo.hex` firmware, the same one you would flash onto a board, blinks the red LED on
D13 and makes the green LED on D6 "breathe" with PWM, while the logic analyser shows the signal.

![SiliconForge: the firmware drives two LEDs on the breadboard, the logic analyser shows the PWM](docs/images/siliconforge.gif)

- **Watch your outputs**: the current of each LED is computed from the circuit (forward voltage, resistor), and a
  PWM-driven LED is lit the way the eye sees it.
- **Try your inputs**: press a button, turn the potentiometer, or click a pin to drive it.
- **Talk to your code**: send text to the microcontroller and read its replies in the serial monitor.
- **Investigate a problem**: pause execution, inspect registers and then step through one instruction.
- **Spot a wiring mistake**: an output wired to a rail, or two opposite outputs, are reported as a short circuit.

## Measure, as on the bench

Every pin change is recorded with the clock cycle it happens on: a 62.5 ns resolution at 16 MHz, with no sampling,
whatever the emulation speed. The **Instruments** panel turns that capture into a logic analyser and an
oscilloscope: frequency and duty cycle, cursors, edge trigger, UART frame decoding, VCD export for GTKWave or
PulseView. See the [instruments guide](docs/INSTRUMENTS.md) (in French).

![The 7-segment counter and the serial frame "3 CR LF" decoded by the logic analyser](docs/images/counter.png)

![The oscilloscope: 3.3 V from the potentiometer on A0 and the 7.8 kHz PWM on D6](docs/images/oscilloscope.png)

## Change the circuit

A circuit is a JSON file: the chip, the components plugged in the holes of the board and the wires. Three are
provided (LEDs and a potentiometer, a 7-segment display, an LED and two buttons); load your own with
`--circuit circuit.json`, **Load circuit…** or drag and drop. The available components are the LED, the resistor, the
push button, the potentiometer, the 7-segment display and the crystal; the [circuits guide](docs/CIRCUITS.md) (in
French) explains how to add one. Graphical editing of the circuit and simulated screens are planned next steps.

## Check the pins

The **Pinout** view shows physical pins, their functions and their Arduino names. Indicators follow the levels
as the program runs. Click an input to drive it high or low, then release it, and observe how your code responds.

![The Pinout view in the light theme: the functions of each pin and its level while the firmware runs](docs/images/pinout.png)

The pinout also labels functions of the physical chip that are not yet simulated.

## What you can test today

| Feature | Available |
| --- | --- |
| Firmware | Intel HEX and ELF loading, run, pause, single stepping, reset and adjustable speed |
| ATmega328P | Instructions and cycle timings, internal interrupts, sleep and self-programming |
| Memories | 32 KiB flash, 2 KiB SRAM and 1 KiB EEPROM |
| Inputs and outputs | Ports B, C and D, pull-ups, logic levels and analogue voltages |
| PWM and timing | Timer0: normal, CTC, Fast PWM and Phase Correct PWM |
| Serial communication | Asynchronous USART0, sending and receiving through the serial monitor, frames visible bit by bit on TXD |
| Circuits | Resistive electrical model: LEDs, resistors, buttons, potentiometers, 7-segment display, short-circuit detection |
| Instruments | Cycle-exact logic analyser and oscilloscope, measurements, cursors, trigger, UART decoder, VCD export |
| Analogue conversion | Single conversions on A0–A5 and the internal reference |
| Automated tests | Control of simulated time, pins, serial communication and circuit components from a C++ test bench |

**Core qualification:** the ATmega328P is **qualified, with documented exceptions**: its 85 instructions are checked
against the AVR manual (cycles of every case), against the simavr simulator (instruction by instruction on random
programs: 150,000 instructions on every build, 74 million in the long campaign) and by C programs compiled at `-O0`,
`-O2` and `-Os`. See the
[qualification matrix (French)](docs/qualification/README.md).

**Current limitations:** Timer1/2, SPI, I²C/TWI, watchdog, analogue comparator and external interrupts are not yet
emulated. Their labels on the pinout do not mean they are available. Further microcontrollers, including the
ATmega2560, as well as ARM and RISC-V targets are planned.

## Try SiliconForge

The project currently builds from source. You need **CMake 3.31+** and a compiler with a **C++26** standard library
(GCC 16 in Linux CI). Dependencies are downloaded automatically during
configuration. Neither Python nor Node.js is needed: the repository's
tools are written in C++ and built with the rest into `build/tools/` ([study, French](docs/PYTHON-STUDY.md)).

On Linux, also install WebKitGTK 6.0 for the native window: `sudo apt install libwebkitgtk-6.0-dev` on Debian/Ubuntu,
or `webkitgtk6.0-devel` on Fedora. Without an available native engine, the application opens the system browser.

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j
./build/apps/siliconforge/siliconforge
```

The **Open an example…** menu loads a [prebuilt firmware](firmware/README.md) with its circuit: LEDs and PWM, the
7-segment counter, buttons, serial echo, Timer0 interrupts. No AVR compiler is needed.

To try your own code, give the path of your ATmega328P binary on the command line, or use **Load firmware…** in the
application. You do not need to flash a physical board.

## Automate your checks

You can run firmware headlessly, accelerate simulated time and collect its serial output:

```bash
./build/apps/siliconforge/siliconforge --headless --speed max --duration 5 firmware/prebuilt/uart_echo.hex
```

A [bench scenario](docs/BANC.md) describes a test in a JSON file versioned with the project: circuit, firmware,
stimuli, bounded waits and expected measurements. `siliconforge-bench` runs it, keeps the report and the trace, and
tells success (0), a missed assertion (1) and an incompatible bench (2) apart by its exit code:

```bash
./build/apps/siliconforge-bench/siliconforge-bench run scenarios/pwm-setpoint.json
```

The [C++ test bench](include/siliconforge/testing/Bench.hpp) lets you send data, change inputs and check replies
or signal periods reproducibly. Plugged in a circuit, it sees what the user sees: `bench.press("button-d2")`,
`bench.component("led-d13").brightness()`. The [firmware tests](tests/integration/FirmwareTests.cpp) provide examples.
Run the project’s tests with:

```bash
ctest --test-dir build --output-on-failure
```

## Further reading

- [Demonstration firmware](firmware/README.md)
- [Microcontroller descriptions and model limitations](docs/MCU-DESCRIPTIONS.md)
- [Component and circuit format](docs/CIRCUITS.md) (in French)
- [Instruments: logic analyser and oscilloscope](docs/INSTRUMENTS.md) (in French)
- [Programmable bench: scenarios and `siliconforge-bench`](docs/BANC.md) (in French)
- [Agent terminal: Claude Code, Codex, OpenCode or Mistral Vibe in the window](docs/TERMINAL.md) (in French)
- [Source editor: read and edit what the agent changed, with the Git differences](docs/EDITOR.md) (in French)
- [Core qualification: method (French)](docs/QUALIFICATION.md) and [matrix per chip](docs/qualification/README.md)
- [Architecture and contributing](docs/ARCHITECTURE.md)
- [WebFront 1.0: upgrade, adoption and decisions (French)](docs/WEBFRONT-1.0.md) (earlier reports: [0.9](docs/WEBFRONT-0.9.md), [0.8](docs/WEBFRONT-0.8.md), [0.7](docs/WEBFRONT-0.7.md))
- [Project direction](docs/adr)

## Licence

SiliconForge is dual-licensed under **Apache-2.0 OR CECILL-B**, at your option. [LICENSE](LICENSE) contains the full
texts, including the French and English versions of CeCILL-B. Third-party components retain their own licences.

WebFront 1.0: [upgrade and adoption (French)](docs/WEBFRONT-1.0.md).
