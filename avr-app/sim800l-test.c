/* ---------------------------------------------------------------------------------------------
 * Test komunikacji AT z SIM800L (albo z symulatorem uart-test/sim800l-simulator.py na RPi)
 *
 * Program cyklicznie wysyła "AT" i czeka na odpowiedź "OK":
 *  - dioda na PD2 ("info"): miga wolno = program działa, czekamy
 *  - dioda na PD3 ("write"): 3 szybkie mignięcia = odebrano poprawne "OK"
 *
 * To krok pośredni między echem UART (attiny-rpi-2way-com.c) a pełnym trackerem
 * (gps-tracker.c): sprawdza wysyłanie komend AT, odbiór linii odpowiedzi i parsowanie.
 * Ten sam kod UART i readLine() co w gps-tracker.c.
 * --------------------------------------------------------------------------------------------- */

#ifndef F_CPU
#define F_CPU 1000000UL   // 8MHz z dzielnikiem /8 = 1MHz (fabryczne fuses ATtiny2313)
#endif

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <util/delay.h>

// Parametry komunikacji UART - jak w attiny-rpi-2way-com.c i gps-tracker.c
#define BAUD 9600
#define MYUBBR ((F_CPU / (BAUD * 8L)) - 1)

#define BUFFER_SIZE 40

const char AT[] PROGMEM   = "AT\r\n";
const char ISOK[] PROGMEM = "OK";

static char response[BUFFER_SIZE];
static char buf[8];

// ---------------------------------------------------------------------------------------------
// UART - identyczne funkcje jak w gps-tracker.c (polling, bez przerwań)
// ---------------------------------------------------------------------------------------------
void initUart(void) {
  UCSRA = (1 << U2X);
  UBRRH = (uint8_t)(MYUBBR >> 8);
  UBRRL = (uint8_t)(MYUBBR);
  UCSRB = (1 << RXEN) | (1 << TXEN);
  UCSRC = (0 << USBS) | (3 << UCSZ0);   // 8N1
}

void sendCharUart(uint8_t charToSend) {
  while (!(UCSRA & (1 << UDRE)));
  UDR = charToSend;
}

uint8_t receiveCharUart(void) {
  while (!(UCSRA & (1 << RXC)));
  return UDR;
}

void sendStringPgm(const char *stringToSend) {
  char c;
  while ((c = pgm_read_byte(stringToSend++)) != 0x00) {
    sendCharUart(c);
  }
}

// Odczyt jednej niepustej linii z UART (pomija puste CR/LF) - jak w gps-tracker.c
uint8_t readLine(void) {
  uint8_t char1;
  uint8_t pos = 0;
  while (1) {
    char1 = receiveCharUart();
    if (char1 != 0x0a && char1 != 0x0d) {
      if (pos < BUFFER_SIZE - 1) response[pos++] = char1;
    } else if (pos > 0) {
      response[pos] = '\0';
      return 1;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Diody LED - PD2 "info" i PD3 "write", jak w attiny-rpi-2way-com.c
// ---------------------------------------------------------------------------------------------
void setupDiode(void) {
  DDRD |= (1 << 2) | (1 << 3);
  PORTD &= ~((1 << 2) | (1 << 3));
}

// 3 szybkie mignięcia diodą PD3 - sygnalizacja odebranego "OK"
void blinkOkDiode(void) {
  for (uint8_t i = 0; i < 3; i++) {
    PORTD |= (1 << 3);
    _delay_ms(120);
    PORTD &= ~(1 << 3);
    _delay_ms(120);
  }
}

int main(void) {
  initUart();
  setupDiode();

  while (1) {
    PORTD |= (1 << 2);    // dioda info: wysyłamy AT
    sendStringPgm(AT);

    readLine();           // czekaj na linię odpowiedzi (blokujące)
    PORTD &= ~(1 << 2);

    memcpy_P(buf, ISOK, sizeof(ISOK));
    if (strstr(response, buf) != 0) {
      blinkOkDiode();     // "OK" odebrane - sukces
    }

    _delay_ms(2000);      // odczekaj i powtórz test
  }
}
