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
| `moveComponent(id, holes)` | Moves terminals of a component or of a wire (`"wire-3"`) to other holes: `moveComponent("r-d13", {b: "h20"})`, within the limits of its footprint |
| `component(id)`, `setComponent(id, name, value)` | State of a component in the last frame (`{brightness}`, `{pressed}`...), and the user's interactions (`"pressed"`, 1) |
| `examples()`, `loadExample(id)` | Built-in firmware with their circuits |
| `instruments` | The capture and its measurements: `sync()`, `samples(pin)`, `measure(pin, from, to)`, `decodeUart(pin)`, `vcd()`; see [the guide](../../docs/INSTRUMENTS.md) |
| `terminal` | The agent terminal (#55): `clients`, `open(id)`, `hangUp()`, `state`, the xterm.js `terminal`; see [the terminal guide](../../docs/TERMINAL.md) |
| `editor` | The editor of the project sources (#56): `files`, `open(path)`, `text`, `save()`, `state`, `conflict`, `keepMine()`, `useDisk()`, `restore()`, `view(mode)`, `poll()`; see [the editor guide](../../docs/EDITOR.md) |
| `bench` | The bench (#60): `list()`, `run(scenarioFile)`, `open(runName)`, the shown `result`, `differences(a, b)`; see [the bench guide](../../docs/BANC.md) |
| `side` | The tabs of the side column (#75): `select("bench" \| "terminal" \| "editor")`, `selected` |
| `agent` | The calls of the agent's MCP tools (#76): `entries`, `enabled`, `drives`, `setDrives(bool)`, `unseen`; see [the agent guide](../../docs/AGENT-MCP.md) |
| `load(name, Uint8Array)` | Load an Intel HEX or ELF firmware |
| `state`, `output`, `pin(name)` | Last frame pushed by the emulator, serial output since reset |
| `waitFor(predicate, ms)`, `waitForSerial(text, ms)`, `waitForPin(name, level, ms)` | Promises resolved on the frame that satisfies them |

Pins are named after the datasheet (`PB5`) or the Arduino Uno board (`D13`).

## Running the suite

On macOS (WKWebView) and Windows (WebView2), only `UI-Terminal`, `UI-TerminalClients` and `UI-Editor` run (#55, #56); they need the desktop session of a logged-in user. `UI-Terminal` and `UI-Editor` adapt their few shell commands to `cmd.exe`.

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
| `UI-Bench` | `bench.spec.js` | none | A scenario of a project directory (made in the build tree) runs from the window with the engine of `siliconforge-bench`; verdict, measurements, steps and trace are shown; a second run reproduces the first byte for byte; the evidence of a failed run is found again by `open`; names with a path are refused with a typed error |
| `UI-Agent` | `agent.spec.js` | `pwm_setpoint.hex`, loaded by the agent | The MCP server of the window (#76): the shell of the terminal, standing for the client, calls the tools with curl (required on PATH); the firmware runs, the button lights D13, the measure gives 75 %, the calls show in the journal and are counted on the Bench tab, "Let the agent drive" refuses a reset and keeps the reads, a scenario run opens in the Bench panel, a wrong token gets 401 |
| `UI-Terminal` | `terminal.spec.js` | none | The shell opens in the project directory in a native pseudoterminal; typed input, the size of the panel and Ctrl+C reach it; the session survives a new attach; closing it leaves the bench usable and a new session can start |
| `UI-Editor` | `editor.spec.js` | none | The sources editor (#56): open, edit and save; a clean buffer reloads after an external change; both versions and the recoverable local text for a buffer with edits; the Git changes a shell leaves (Linux only; its project directory is remade by `UI-Editor-Project`) |
| `UI-Foundry` | `foundry.spec.js` | built by the agent | The PWM correction loop of [docs/FOUNDRY.md](../../docs/FOUNDRY.md) (#61), in a project remade by `UI-Foundry-Project` (needs avr-gcc and Git): the shell, standing for the agent, applies `tests/foundry/1-setpoint-off.patch`, builds with `make -C firmware prebuilt` and runs `siliconforge-bench`, which fails on D6; the change is found in the editor diff, the failed run in the Bench panel with its measurement and trace; after `2-disconnect-oc0a.patch` the bench passes and a run from the window gives the same report |
| `UI-FoundryClient` | `foundry-client.spec.js` | built by the client | Opt-in (`-DSILICONFORGE_FOUNDRY_CLIENT=ON`, uses the account of the user): the exercise of docs/FOUNDRY.md with a real client in the terminal; its questions are relayed to an operator through `.siliconforge/relay/` of the project; once the bench passes, the diff, the run and a rerun from the window; delay and answers in `result.txt`. Kept in Xvfb (`GDK_BACKEND=x11`, no `WAYLAND_DISPLAY`) |
| `UI-TerminalClients` | `terminal-clients.spec.js` | none | Opt-in qualification: each agent client installed on the machine is opened, observed and closed (docs/TERMINAL.md); a client not installed is skipped |
| `UI-Files` | `files.spec.js` | `blink.hex` | Firmware and MCU description chosen through the page's file inputs, failures shown in the page (no `alert()`), typed `error.code` of `sf.selectMcu` / `sf.loadMcu` / `sf.load`, and a description and chip larger than 64 KiB. The system file dialog is not driven: the spec gives the input a `File` and dispatches `change`. Fixtures come from `firmware/prebuilt/blink.hex` and `mcus/atmega328p.json` |
| `UI-Instruments` | `instruments.spec.js` | `demo.hex` | The capture in the page: half-periods of D13 equal to within the firmware's own jitter, 7812.5 Hz PWM on D6 and its duty cycle at two points of the ramp, traces drawn on the canvas, single trigger on a recorded rising edge, cursors placed with the pointer, VCD export, capture restarted by a reset |
| `UI-Circuits` | `circuits.spec.js` | `counter.hex`, `--circuit seven-segment` | The 7-segment display shows each digit the firmware counts, the UART frames of D1 decode to the serial output at an auto-detected 9600 baud, typed errors of `setComponent`, an example switching circuit and firmware, buttons lighting the LED through the electrical model, a circuit chosen with the file input, a resistor outlined, stretched with the pointer up to its maximum length and refused beyond it, an invalid circuit reported with its JSON path, the short-circuit banner, a bare board for another MCU |
| `UI-Mcu` | `mcu.spec.js` | `blink.hex` | MCU descriptions: the catalog chip, a modified file sharing a catalog name, no repeated writes of the selector, exact server IDs, an ATmega168 chosen with the file input and running `blink`, the JSON path of an invalid description, a retried chip request, the generic pin labels of a chip without a circuit, the return to the catalog. Formerly a manual Playwright scenario |
| `UI-Tour` | `tour.spec.js` | built-in examples | Screenshots and a 4 s sequence of the real window (`captures/*.png`, `captures/demo/`), each backed by an assertion. `build/tools/captures2gif` turns the sequence into the README animation |
| `UI-LedRealtime` | `led-realtime.spec.js` | `blink.hex` | At ×1, mean period 1 s ± 10 % on `performance.now()` over 4.5 s, no missed edge (each half-period 0.5 ± 0.15 s). One retry on failure for loaded CI machines |

The JSON reports, including individual spec verdicts, are in
`build/tests/webfront-jasmine/UI-*/report.json`. Jasmine 7.0.2 is downloaded and verified at configuration; for offline
configuration, set `WEBFRONT_JASMINE_ARCHIVE_DIR` to a directory containing `jasmine-core-7.0.2.tgz`.
See WebFront's [consumer testing recipe](https://github.com/ambroise-leclerc/WebFront/blob/develop/docs/CONSUMER_TESTING.md).

The same API can be used by any browser automation tool (Playwright, WebDriver) to test a firmware's behaviour from
the outside.
