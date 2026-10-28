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

The specs are meant for WebFront's `wf test` command, which runs Jasmine specs in the application's window and exits
with a non-zero code on failure. Wiring `wf test` into CTest (the application in developer mode, as WebFront's
`examples/life/LifeDeveloper.cpp` does) is the next step of the roadmap; until then the suite can be run by hand:

1. `build/apps/siliconforge/siliconforge firmware/prebuilt/uart_echo.hex`
2. In the page's developer console, load Jasmine and `board.spec.js`, or run them from a WebFront developer project
   (`wf install jasmine-core@7.0.2`, then list `tests/board.spec.js` under `"test": {"specs": [...]}`).

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
a failed chip request, checks generic pin labels, and returns to the built-in chip.
The CTest count reported for the C++ tests does not include this scenario.
