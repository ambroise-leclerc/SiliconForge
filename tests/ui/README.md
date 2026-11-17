# UI tests

`board.spec.js` is a [Jasmine](https://jasmine.github.io/) suite that runs **inside** the SiliconForge page, against a
live emulated MCU. It drives the firmware through `window.siliconForge`, the automation API exposed by
[`ui/siliconforge.js`](../../ui/siliconforge.js):

| Call | Effect |
| --- | --- |
| `run()`, `pause()`, `step()`, `reset()`, `setSpeed(factor)` | Simulation controls (`0` = as fast as possible) |
| `send(text)` | Bytes received by USART0 |
| `setPin(name, 1 \| 0 \| null)`, `setVoltage(name, volts)` | Drive or release an input (a probe on its net), apply an analog voltage (turns the potentiometer that feeds the pin, if any) |
| `circuit()`, `circuits()`, `selectCircuit(name)`, `loadCircuit(name, text)` | The circuit the chip is plugged in, the built-in ones, and changing it |
| `component(id)`, `setComponent(id, name, value)` | State of a component in the last frame (`{brightness}`, `{pressed}`...), and the user's interactions (`"pressed"`, 1) |
| `examples()`, `loadExample(id)` | Built-in firmware with their circuits |
| `instruments` | The capture and its measurements: `sync()`, `samples(pin)`, `measure(pin, from, to)`, `decodeUart(pin)`, `vcd()`; see [the guide](../../docs/INSTRUMENTS.md) |
| `load(name, Uint8Array)` | Load an Intel HEX or ELF firmware |
| `state`, `output`, `pin(name)` | Last frame pushed by the emulator, serial output since reset |
| `waitFor(predicate, ms)`, `waitForSerial(text, ms)`, `waitForPin(name, level, ms)` | Promises resolved on the frame that satisfies them |

Pins are named after the datasheet (`PB5`) or the Arduino Uno board (`D13`).

## Running the suite

On Linux, install `libwebkitgtk-6.0-dev`, `xvfb` and `xauth` before configuring. Build with the application and tests
enabled, then run:

```sh
cmake --build build
ctest --test-dir build -L ui --output-on-failure
```

WebFront runs the specs in native WebKitGTK windows under Xvfb, against the firmware built from `firmware/src` (`firmware/prebuilt` without avr-gcc):

| Test | Spec | Firmware | Checks |
| --- | --- | --- | --- |
| `UI-Uart` | `board.spec.js` | `uart_echo.hex` | Serial console and controls |
| `UI-Blink` | `blink.spec.js` | `blink.hex` | D13 toggles and the pin view follows |
| `UI-Led` | `led.spec.js` | `blink.hex` | Breadboard LED `.bb-led[data-pin="D13"]`, on the simulated clock. At ×5 (a frame covers ~0.17 s, under a half-period) the frame's `data-level`, the high fraction, places each edge: every half-period is 0.5 s ± 1 %. At full speed a frame holds several edges, so the pin's `edges` counts are summed over the longest run of consecutive frames (`state.frame`): the count fixes the half-period to one edge (~2 % over 24 s) and the LED level of every frame must match the blink computed in closed form. Frames skipped by a slow client are excluded
| `UI-Files` | `files.spec.js` | `blink.hex` | Firmware and MCU description chosen through the page's file inputs, failures shown in the page (no `alert()`), typed `error.code` of `sf.selectMcu` / `sf.loadMcu` / `sf.load`, and a description and chip larger than 64 KiB. The system file dialog is not driven: the spec gives the input a `File` and dispatches `change`. Fixtures come from `firmware/prebuilt/blink.hex` and `mcus/atmega328p.json` |
| `UI-Instruments` | `instruments.spec.js` | `demo.hex` | The capture in the page: half-periods of D13 equal to within the firmware's own jitter, 7812.5 Hz PWM on D6 and its duty cycle at two points of the ramp, traces drawn on the canvas, single trigger on a recorded rising edge, cursors placed with the pointer, VCD export, capture restarted by a reset |
| `UI-Circuits` | `circuits.spec.js` | `counter.hex`, `--circuit seven-segment` | The 7-segment display shows each digit the firmware counts, the UART frames of D1 decode to the serial output at an auto-detected 9600 baud, typed errors of `setComponent`, an example switching circuit and firmware, buttons lighting the LED through the electrical model, a circuit chosen with the file input, an invalid circuit reported with its JSON path, the short-circuit banner, a bare board for another MCU |
| `UI-Tour` | `tour.spec.js` | built-in examples | Screenshots and a 4 s sequence of the real window (`captures/*.png`, `captures/demo/`), each backed by an assertion. `tools/captures2gif` turns the sequence into the README animation |
| `UI-LedRealtime` | `led-realtime.spec.js` | `blink.hex` | At ×1, mean period 1 s ± 10 % on `performance.now()` over 4.5 s, no missed edge (each half-period 0.5 ± 0.15 s). One retry on failure for loaded CI machines |

The JSON reports, including individual spec verdicts, are in
`build/tests/webfront-jasmine/UI-*/report.json`. Jasmine 7.0.2 is downloaded and verified at configuration; for offline
configuration, set `WEBFRONT_JASMINE_ARCHIVE_DIR` to a directory containing `jasmine-core-7.0.2.tgz`.
See WebFront's [consumer testing recipe](https://github.com/ambroise-leclerc/WebFront/blob/develop/docs/CONSUMER_TESTING.md).

The same API can be used by any browser automation tool (Playwright, WebDriver) to test a firmware's behaviour from
the outside.

## Dynamic MCU smoke test

This is a **manual integration test**, outside CTest and CI. Use a dedicated local
server: the script replaces its MCU, runs firmware, and clears its current state.
It is not suitable for a server used by another session.

```sh
build/apps/siliconforge/siliconforge --no-browser --port 19032
SILICONFORGE_URL=http://127.0.0.1:19032 node /path/to/SiliconForge/tests/ui/mcu.playwright.cjs
```

Install Playwright in the test environment (or set `SILICONFORGE_PLAYWRIGHT_MODULE`
to its module directory). `PLAYWRIGHT_CHROMIUM_EXECUTABLE` optionally selects the
browser executable. Fixture paths resolve relative to the script, so the Node
command works from any working directory.

It was last run against the interface of WebFront 0.9; its expectations were adapted to the circuits of ADR 0005
(a loaded MCU gets a bare board with its chip) but **not run again** since.

The test uploads an ATmega168 description, checks the selected MCU, dynamic
breadboard and pinout, runs `blink`, verifies a localized validation error, retries
a failed chip request and clears its error, checks generic pin labels, distinguishes
a modified file sharing a catalog name, verifies exact server IDs and avoids repeated
selection writes, then returns to the built-in chip.
The CTest count reported for the C++ tests does not include this scenario.
