# SiliconForge

[Français](README.md) · English (UK)

**Test your embedded code straight away, without flashing a board.**

Load the `.hex` or `.elf` binary produced by your toolchain and run it on a simulated microcontroller. With
SiliconForge, you can check an LED blink, exchange serial messages, try an analogue input or inspect registers
without connecting any hardware. Pause execution, step through instructions and repeat as often as you need.

SiliconForge is a native, cross-platform application written in C++, for **macOS, Linux and Windows**.
The first supported microcontroller is the **16 MHz ATmega328P**, used in the Arduino Uno.

## Your code, a circuit, a visible result

The breadboard brings together a microcontroller and simulated components. In this example, the same `blink.hex`
firmware you would flash onto a board makes the D13 LED blink. Simulated time, cycles and registers update as
the program runs.

![SiliconForge on macOS: firmware blinking an LED on the breadboard](docs/images/siliconforge.gif)

The demonstration circuit already includes two LEDs, resistors, a push button and a potentiometer:

- **Watch your outputs**: the D13 LED follows the program and the D6 LED reflects the PWM duty cycle.
- **Try your inputs**: press the button connected to D2 or turn the potentiometer on A0.
- **Talk to your code**: send text to the microcontroller and read its replies in the serial monitor.
- **Investigate a problem**: pause execution, inspect registers and then step through one instruction.

The aim is to let you **connect your own components and build a circuit around the microcontroller**. Today,
the demonstration components are prewired; adding components and wiring them freely are planned next steps.
Simulated displays and screens are also planned.

## Check the pins

The **Pinout** view shows physical pins, their functions and their Arduino names. Indicators follow the levels
as the program runs. Click an input to drive it high or low, then release it, and observe how your code responds.

![SiliconForge on macOS: pin levels and registers while the firmware runs](docs/images/pinout.gif)

The pinout also labels functions of the physical chip that are not yet simulated.

## What you can test today

| Feature | Available |
| --- | --- |
| Firmware | Intel HEX and ELF loading, run, pause, single stepping, reset and adjustable speed |
| ATmega328P | Instructions and cycle timings, internal interrupts, sleep and self-programming |
| Memories | 32 KiB flash, 2 KiB SRAM and 1 KiB EEPROM |
| Inputs and outputs | Ports B, C and D, pull-ups, logic levels and analogue voltages |
| PWM and timing | Timer0: normal, CTC, Fast PWM and Phase Correct PWM |
| Serial communication | Asynchronous USART0, sending and receiving through the serial monitor |
| Analogue conversion | Single conversions on A0–A5 and the internal reference |
| Automated tests | Control of simulated time, pins and serial communication from a C++ test bench |

**Current limitations:** Timer1/2, SPI, I²C/TWI, watchdog, analogue comparator and external interrupts are not yet
emulated. Their labels on the pinout do not mean they are available. Further microcontrollers, including the
ATmega2560, as well as ARM and RISC-V targets are planned.

## Try SiliconForge

The project currently builds from source. You need **CMake 3.31+** and a compiler with a **C++26** standard library
(GCC 16 in Linux CI, Apple Clang 21 for the macOS captures). Dependencies are downloaded automatically during
configuration.

On Linux, also install WebKitGTK 6.0 for the native window: `sudo apt install libwebkitgtk-6.0-dev` on Debian/Ubuntu,
or `webkitgtk6.0-devel` on Fedora. Without an available native engine, the application opens the system browser.

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j
./build/apps/siliconforge/siliconforge firmware/prebuilt/blink.hex
```

The [prebuilt examples](firmware/README.md) let you try blinking, serial echo, Timer0 interrupts and PWM straight
away, without installing an AVR compiler.

To try your own code, replace the `blink.hex` path with your ATmega328P binary, or use **Load firmware…** in the
application. You do not need to flash a physical board.

## Automate your checks

You can run firmware headlessly, accelerate simulated time and collect its serial output:

```bash
./build/apps/siliconforge/siliconforge --headless --speed max --duration 5 firmware/prebuilt/uart_echo.hex
```

The [C++ test bench](include/siliconforge/testing/Bench.hpp) lets you send data, change inputs and check replies
or signal periods reproducibly. The [firmware tests](tests/integration/FirmwareTests.cpp) provide examples.
Run the project’s tests with:

```bash
ctest --test-dir build --output-on-failure
```

## Further reading

- [Demonstration firmware](firmware/README.md)
- [Microcontroller descriptions and model limitations](docs/MCU-DESCRIPTIONS.md)
- [Architecture and contributing](docs/ARCHITECTURE.md)
- [Project direction](docs/adr)

## Licence

SiliconForge is dual-licensed under **Apache-2.0 OR CECILL-B**, at your option. [LICENSE](LICENSE) contains the full
texts, including the French and English versions of CeCILL-B. Third-party components retain their own licences.
