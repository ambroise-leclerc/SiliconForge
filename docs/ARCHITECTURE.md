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

`mcu/atmega328p/Atmega328p` composes the AVR core with the ATmega328P memory sizes, its ports, Timer0, USART0, ADC
and EEPROM, and labels the pins after the Arduino Uno board (D0-D13, A0-A5). `mcu::create("atmega328p")` builds it
by name; the registry is the only place that knows the list of chips.

### Adding a chip or an architecture

1. A megaAVR chip: a new `mcu/<chip>/` reusing `arch/avr`, registered in `src/mcu/Registry.cpp`.
2. A new architecture: `arch/<family>/` with its core and peripherals, then a chip in `mcu/`. Nothing changes in
   `core/`, the application, the bench or the interface, which only see `Mcu`.

## Application and interface

- `Simulation` runs the MCU on its own thread, by slices of one simulated millisecond, paced against the wall clock
  (real time or a multiple) or as fast as possible. Other threads access the MCU through `Simulation::access()`.
- `WebBridge` registers the `sf.*` C++ functions called by the page, and pushes the state about 30 times per second
  with `siliconForge.onState(json)`. Each pin carries the fraction of the frame it spent high: a PWM-driven LED is
  shown with its average brightness.
- `ui/` is plain HTML/CSS/JavaScript served from a document root. `window.siliconForge` is both the page's model and
  the automation API used by UI tests.

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
| Firmware | `tests/integration/FirmwareTests.cpp` | Real avr-gcc binaries checked through the bench (1 Hz blink, echo, ISR, PWM) |
| Interface | `tests/ui/` | Jasmine specs driving the page and the firmware through `window.siliconForge` |
