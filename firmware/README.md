# Test firmware

Small avr-gcc programs for the ATmega328P (Arduino Uno, 16 MHz) used by the integration tests
(`tests/integration/FirmwareTests.cpp`) and as demonstrations.

| Firmware | Exercises |
| --- | --- |
| `blink` | GPIO, PINx toggle, cycle-exact busy-wait loops (`_delay_ms`): the LED on D13 must blink at 1 Hz |
| `uart_echo` | USART0 polling at 9600 bauds: prints `ready`, echoes each line in upper case |
| `timer0_isr` | Timer0 CTC, compare-match interrupt, idle sleep: prints `t=<ms>` every second |
| `demo` | The demonstration circuit: red LED on D13 blinks at 1 Hz, green LED on D6 breathes with Timer0 PWM (2 s period) |
| `pwm` | Timer0 Fast PWM on D6 (25 %) and D5 (75 %) |
| `counter` | The 7-segment circuit (`circuits/seven-segment.json`): counts 0 to 9 every 500 ms, each digit also sent at 9600 bauds |
| `buttons` | The `led-buttons` circuit: inputs with pull-ups, the LED on D9 follows the D2 button and is latched by D3 |

The binaries of `prebuilt/` are also embedded in the application, which offers them with their circuit in its
**Example** menu.

When avr-gcc is installed, CMake builds these sources (target `firmware`, output in `<build>/firmware`) and the tests and
UI specs run on those binaries, so editing a `.c` file is visible in the next test run. Without avr-gcc, CMake says so and
the tests fall back to the binaries in `prebuilt/`, which are committed so that they run without an AVR toolchain. The CI
fails if `prebuilt/` differs from what the sources produce. `blink_250ms` is `blink.c` built with
`-DBLINK_HALF_PERIOD_MS=250`; it only exists in the build directory and checks that a source change reaches the LED. To
refresh the committed binaries:

```bash
sudo apt install gcc-avr avr-libc binutils-avr   # Debian/Ubuntu
make -C firmware prebuilt
```
