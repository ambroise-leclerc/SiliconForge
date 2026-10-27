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

1. `build/dev/apps/siliconforge/siliconforge firmware/prebuilt/uart_echo.hex`
2. In the page's developer console, load Jasmine and `board.spec.js`, or run them from a WebFront developer project
   (`wf install jasmine-core@7.0.2`, then list `tests/board.spec.js` under `"test": {"specs": [...]}`).

The same API can be used by any browser automation tool (Playwright, WebDriver) to test a firmware's behaviour from
the outside.
