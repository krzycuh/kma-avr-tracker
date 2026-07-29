/* ---------------------------------------------------------------------------------------------
 * Test komunikacji AT: ATmega328P <-> SIM7000E (albo symulator uart-test/sim7000-simulator.py)
 *
 * Program cyklicznie wysyła "AT" i czeka na odpowiedź "OK":
 *  - dioda na PD2 ("info"): świeci podczas wysyłania/oczekiwania
 *  - dioda na PD3 ("write"): 3 szybkie mignięcia = odebrano poprawne "OK"
 *
 * To krok pośredni między echem UART a pełnym trackerem (gps-tracker.c):
 * sprawdza wysyłanie komend AT, odbiór linii odpowiedzi i parsowanie.
 * Ten sam kod UART i readLine() co w gps-tracker.c (rejestry UART0 ATmega328P).
 * --------------------------------------------------------------------------------------------- */

#ifndef F_CPU
#define F_CPU 1000000UL   // 8MHz z dzielnikiem /8 = 1MHz (fabryczne fuses ATmega328P)
#endif

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <util/delay.h>

// Parametry komunikacji UART - jak w gps-tracker.c
#define BAUD 9600
#define MYUBBR ((F_CPU / (BAUD * 8L)) - 1)

#define BUFFER_SIZE 40

// Limit czekania na pojedynczy znak z UART: ~5 s przy 1 MHz
#define RX_TIMEOUT_LOOPS 500000UL

const char AT[] PROGMEM   = "AT\r\n";
const char ISOK[] PROGMEM = "OK";

static char response[BUFFER_SIZE];
static char buf[8];
static uint8_t rxOk;

// ---------------------------------------------------------------------------------------------
// UART0 - identyczne funkcje jak w gps-tracker.c (polling, timeout ~5s na znak)
// ---------------------------------------------------------------------------------------------
void initUart(void) {
  UCSR0A = (1 << U2X0);
  UBRR0H = (uint8_t)(MYUBBR >> 8);
  UBRR0L = (uint8_t)(MYUBBR);
  UCSR0B = (1 << RXEN0) | (1 << TXEN0);
  UCSR0C = (0 << USBS0) | (3 << UCSZ00);   // 8N1
}

void sendCharUart(uint8_t charToSend) {
  while (!(UCSR0A & (1 << UDRE0)));
  UDR0 = charToSend;
}

uint8_t receiveCharUart(void) {
  uint32_t i = 0;
  while (!(UCSR0A & (1 << RXC0))) {
    if (++i > RX_TIMEOUT_LOOPS) {
      rxOk = 0;
      return 0;
    }
  }
  return UDR0;
}

void sendStringPgm(const char *stringToSend) {
  char c;
  while ((c = pgm_read_byte(stringToSend++)) != 0x00) {
    sendCharUart(c);
  }
}

// Odczyt jednej niepustej linii z UART (pomija puste CR/LF); 0 przy timeoucie
uint8_t readLine(void) {
  uint8_t char1;
  uint8_t pos = 0;
  rxOk = 1;
  while (1) {
    char1 = receiveCharUart();
    if (!rxOk) return 0;
    if (char1 != 0x0a && char1 != 0x0d) {
      if (pos < BUFFER_SIZE - 1) response[pos++] = char1;
    } else if (pos > 0) {
      response[pos] = '\0';
      return 1;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Diody LED - PD2 "info" i PD3 "write"
// ---------------------------------------------------------------------------------------------
void setupDiode(void) {
  DDRD |= (1 << PD2) | (1 << PD3);
  PORTD &= ~((1 << PD2) | (1 << PD3));
}

// 3 szybkie mignięcia diodą PD3 - sygnalizacja odebranego "OK"
void blinkOkDiode(void) {
  for (uint8_t i = 0; i < 3; i++) {
    PORTD |= (1 << PD3);
    _delay_ms(120);
    PORTD &= ~(1 << PD3);
    _delay_ms(120);
  }
}

int main(void) {
  initUart();
  setupDiode();

  while (1) {
    PORTD |= (1 << PD2);    // dioda info: wysyłamy AT i czekamy
    sendStringPgm(AT);

    if (readLine()) {
      memcpy_P(buf, ISOK, sizeof(ISOK));
      if (strstr(response, buf) != 0) {
        blinkOkDiode();     // "OK" odebrane - sukces
      }
    }
    PORTD &= ~(1 << PD2);

    _delay_ms(2000);        // odczekaj i powtórz test
  }
}
