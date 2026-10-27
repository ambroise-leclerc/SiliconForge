# Test firmware

Small avr-gcc programs for the ATmega328P (Arduino Uno, 16 MHz) used by the integration tests
(`tests/integration/FirmwareTests.cpp`) and as demonstrations.

| Firmware | Exercises |
| --- | --- |
| `blink` | GPIO, PINx toggle, cycle-exact busy-wait loops (`_delay_ms`): the LED on D13 must blink at 1 Hz |
| `uart_echo` | USART0 polling at 9600 bauds: prints `ready`, echoes each line in upper case |
| `timer0_isr` | Timer0 CTC, compare-match interrupt, idle sleep: prints `t=<ms>` every second |
| `pwm` | Timer0 Fast PWM on D6 (25 %) and D5 (75 %) |

The binaries in `prebuilt/` are committed so that the tests run without an AVR toolchain. To rebuild them:

```bash
sudo apt install gcc-avr avr-libc binutils-avr   # Debian/Ubuntu
make -C firmware prebuilt
```
