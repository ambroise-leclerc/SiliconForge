# Architecture

SiliconForge is organised in layers that only depend on the layers below them.

```
 ┌──────────────────────────────────────────────────────────────┐
 │ apps/siliconforge   CLI, Simulation thread, WebBridge         │──── WebFront ──── ui/ (browser)
 ├──────────────────────────────────────────────────────────────┤
 │ testing/Bench       Test bench (Catch2 or any framework)      │
 ├──────────────────────────────────────────────────────────────┤
 │ mcu/<chip>          Chip = core + memory map + peripherals    │  SiliconForge::mcu
 ├──────────────────────────────────────────────────────────────┤
 │ arch/<family>       CPU core, instruction set, peripherals    │  SiliconForge::avr
 ├──────────────────────────────────────────────────────────────┤
 │ core/               Mcu, Scheduler, Pin, SerialPort,          │  SiliconForge::core
 │                     Debugger, ProgramImage loaders            │
 └──────────────────────────────────────────────────────────────┘
```

## core: what every architecture shares

- **`Mcu`** (`core/Mcu.hpp`) is the only interface the application, the web interface and the test bench know:
  load, reset, `run(cycles)`, `step()`, pins, serial ports, registers, memories, debugger, symbols. It is a virtual
  interface because it is crossed a few times per simulated millisecond, never per instruction.
- **`Scheduler`** is a time-ordered queue of events expressed in clock cycles. Peripherals never tick on every
  cycle: they schedule their next observable change (compare match, end of a serial frame, end of an ADC conversion)
  and bring their state up to date lazily when firmware reads a register. See [ADR 0002](adr/0002-modele-temporel.md).
- **`Pin`** is shared between the MCU (output driver, pull-up) and the outside world (external level or analog
  voltage). Observers receive the cycle of every change, which is how the bench measures periods and duty cycles.
- **`SerialPort`** is the host side of a UART: bytes sent by the host are received at the configured baud rate,
  bytes transmitted by the MCU are kept and notified.
- **`Debugger`**: breakpoints (program addresses), watchpoints (data addresses), thread-safe stop requests.
- **Loaders**: Intel HEX and ELF32 (program headers at their load address, function symbols), returning
  `std::expected<ProgramImage, LoadError>`.

## arch/avr: the AVR family

- **`DataSpace`** maps registers, I/O and SRAM. I/O registers can have read/write handlers; a write handler receives
  the mask of the bits written, so that SBI/CBI only act on their bit, as on the ATmega328P (writing PINx toggles,
  writing TIFRx clears flags).
- **`AvrCore`** executes one instruction per iteration through a decoding table of 65,536 handlers indexed by the
  opcode (`Instructions.cpp`). Each handler returns its cycle count, so taken branches, skips over one- or two-word
  instructions and multi-cycle memory accesses are all accounted for. Self-programming writes flash without any
  decoded cache to invalidate.
- **`InterruptController`**: one line per vector, kept up to date by the peripherals (`flag && enable`), the lowest
  vector wins; an acknowledge hook clears the flags the hardware clears when the vector executes.
- **Peripherals** (`GpioPort`, `Timer8`, `Usart`) are parameterised by their register addresses and vectors, so the
  same classes serve every megaAVR that has them.

## mcu: chips

`mcu::create("atmega328p")` loads the embedded `mcus/atmega328p.json`. The loader validates it against
`schemas/mcu.schema.json`, checks references, then assembles `avr::Device` through named peripheral factories.
The JSON supplies memories, register addresses and resets, interrupt vectors, instruction/peripheral timings and
package pins. `mcu/atmega328p/Atmega328p` remains a compatibility facade. Dynamic descriptions use the same loader;
see [the format and import guide](MCU-DESCRIPTIONS.md).

### Adding a chip or an architecture

1. A megaAVR chip: a JSON description loaded through `--mcu-file`, `--mcu-path`, or the interface, reusing `arch/avr`.
2. A new architecture: `arch/<family>/` with its core and peripherals, then a chip in `mcu/`. Nothing changes in
   `core/`, the application, the bench or the interface, which only see `Mcu`.

## circuit: components and circuits

`SiliconForge::circuit` loads what is plugged around the chip ([ADR 0005](adr/0005-composants-et-circuits.md)):
component types from `components/<type>/component.json` and circuits from `circuits/<name>.json`, each validated by
its JSON Schema and then checked for consistency (known types and terminals, footprints, holes used once, property
bounds). `circuit::Library::builtin()` holds the embedded types and `circuits/arduino-demo.json` is the embedded
demonstration circuit. Errors carry a JSON Pointer, like the MCU loader, through the shared `DescriptionError`.
See [the format guide](CIRCUITS.md). The electrical model and the bench access come with #19; until the interface
renders circuits (#20), the breadboard view still draws the `breadboard` section of the MCU description.

## Application and interface

- `Simulation` runs the MCU on its own thread, by slices of one simulated millisecond, paced against the wall clock
  (real time or a multiple) or as fast as possible. Other threads access the MCU through `Simulation::access()`.
- `WebBridge` registers the `sf.*` C++ functions called by the page, and pushes the state about 30 times per second
  with `siliconForge.onState(json)`. Each pin carries the fraction of the frame it spent high: a PWM-driven LED is
  shown with its average brightness. `sf.selectMcu`, `sf.loadMcu` and `sf.loadFirmware` reject their Promise with a
  `webfront::BridgeError` whose `code` is one of the `siliconforge.*` codes of `WebBridge.hpp`; strings crossing the
  bridge have no 64 KiB limit since WebFront 0.9, and a frame only caps the serial bytes it carries.
- `main.cpp` shows the interface in a native window (`webfront::frontend::NativeWebViewFrontend`: WebKitGTK, WebView2
  or WKWebView) when WebFront found the platform's engine at configuration time, and falls back to the system browser
  when it was not built or no graphical session is available. The application code is the same for both: `WebBridge`
  is a template over the WebFront application type.
- `ui/` is plain HTML/CSS/JavaScript served from a document root. `window.siliconForge` is both the page's model and
  the automation API used by UI tests.
- The board views are generic and driven by the JSON description served through `sf.chip()`: package pins and
  labels for the **pinout** view, and the optional demonstration circuit for the **breadboard** view. A dynamically
  loaded MCU needs no JavaScript file. Replacing a chip under the simulation lock rebuilds its observers and the
  browser views, pauses execution and clears the firmware.

## Threads and locks

The emulation thread holds the simulation lock while it runs a slice. WebFront's server thread calls the `sf.*`
functions, which take the same lock. The state pusher builds its frame under the lock, then calls WebFront without
holding any lock: WebFront's server thread runs our callbacks, so calling into WebFront while holding one of our locks
could deadlock.

## Tests

| Level | Location | What |
| --- | --- | --- |
| Instruction | `tests/unit/AvrInstructionTests.cpp` | Results, flags and cycles of each instruction family, encoded with `tests/support/AvrAssembler.hpp` |
| Peripheral | `tests/unit/*Tests.cpp` | GPIO, Timer0 modes, USART timing, EEPROM, ADC, interrupts, sleep |
| Descriptions | `tests/unit/DescriptionTests.cpp`, `tests/unit/CircuitDescriptionTests.cpp` | Committed MCU, component and circuit files are valid; each consistency rule reports its JSON Pointer |
| Firmware | `tests/integration/FirmwareTests.cpp` | Real avr-gcc binaries checked through the bench (1 Hz blink, echo, ISR, PWM) |
| Interface | `tests/ui/` | Jasmine specs driving the page and the firmware through `window.siliconForge` |
