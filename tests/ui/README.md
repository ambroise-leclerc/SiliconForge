# UI tests

`board.spec.js` is a [Jasmine](https://jasmine.github.io/) suite that runs **inside** the SiliconForge page, against a
live emulated MCU. It drives the firmware through `window.siliconForge`, the automation API exposed by
[`ui/siliconforge.js`](../../ui/siliconforge.js):

| Call | Effect |
| --- | --- |
| `run()`, `pause()`, `step()`, `reset()`, `setSpeed(factor)` | Simulation controls (`0` = as fast as possible) |
| `send(text)` | Bytes received by USART0 |
| `setPin(name, 1 \| 0 \| null)`, `setVoltage(name, volts)` | Drive or release an input, apply an analog voltage |
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
| `UI-LedRealtime` | `led-realtime.spec.js` | `blink.hex` | At ×1, mean period 1 s ± 10 % on `performance.now()` over 4.5 s, no missed edge (each half-period 0.5 ± 0.15 s). One retry on failure for loaded CI machines |

The JSON reports, including individual spec verdicts, are in
`build/tests/webfront-jasmine/UI-*/report.json`. Jasmine 7.0.2 is downloaded and verified at configuration; for offline
configuration, set `WEBFRONT_JASMINE_ARCHIVE_DIR` to a directory containing `jasmine-core-7.0.2.tgz`.
See WebFront's [consumer testing recipe](https://github.com/ambroise-leclerc/WebFront/blob/v0.9.0/docs/CONSUMER_TESTING.md).

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

The test uploads an ATmega168 description, checks the selected MCU, dynamic
breadboard and pinout, runs `blink`, verifies a localized validation error, retries
a failed chip request and clears its error, checks generic pin labels, distinguishes
a modified file sharing a catalog name, verifies exact server IDs and avoids repeated
selection writes, then returns to the built-in chip.
The CTest count reported for the C++ tests does not include this scenario.
