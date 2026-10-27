// Prints "ready", then echoes each line received on USART0 in upper case (9600 bauds, 8N1, polling).
#include <avr/io.h>
#include <ctype.h>
#include <stddef.h>

#define BAUD 9600
#include <util/setbaud.h>

static void uartInit(void) {
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;
#if USE_2X
    UCSR0A |= _BV(U2X0);
#else
    UCSR0A &= ~_BV(U2X0);
#endif
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);
}

static void uartPut(char c) {
    loop_until_bit_is_set(UCSR0A, UDRE0);
    UDR0 = c;
}

static void uartPuts(const char* text) {
    while (*text)
        uartPut(*text++);
}

static char uartGet(void) {
    loop_until_bit_is_set(UCSR0A, RXC0);
    return UDR0;
}

int main(void) {
    char   line[64];
    size_t length = 0;

    uartInit();
    uartPuts("ready\r\n");
    for (;;) {
        const char c = uartGet();
        if (c == '\r' || c == '\n') {
            line[length] = '\0';
            uartPuts(line);
            uartPuts("\r\n");
            length = 0;
        } else if (length < sizeof line - 1) {
            line[length++] = (char)toupper((unsigned char)c);
        }
    }
}
