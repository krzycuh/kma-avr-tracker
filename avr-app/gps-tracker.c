/* ---------------------------------------------------------------------------------------------
 * GPS tracker na ATmega328P + SIM7000E (Waveshare NB-IoT/LTE/GPRS/GNSS HAT)
 * Wersja z raportowaniem HTTP (REST) i prawdziwym GPS.
 *
 * Zasada działania:
 *  1. Po starcie budzi moduł (impuls na PWRKEY jeśli milczy), konfiguruje go
 *     (stała prędkość 9600, echo off, PIN, tryb sieci LTE-M/NB-IoT/GSM auto),
 *     włącza odbiornik GNSS i czeka na zalogowanie do sieci (AT+CGATT?).
 *  2. Co REPORT_INTERVAL_MIN minut:
 *     - pyta GNSS o pozycję (AT+CGNSINF) aż będzie fix (do ~3 min),
 *     - aktywuje połączenie danych (AT+CNACT) i wysyła HTTP GET na serwer:
 *       API_SERVER API_PATH?token=API_TOKEN&lat=<szer>&lon=<dług>
 *     - po odpowiedzi HTTP 200 miga diodą na PD3,
 *     - dezaktywuje połączenie danych i czeka do następnego raportu.
 *  3. Przy braku sieci: tryb samolotowy na 30 min (oszczędzanie baterii w garażu).
 *
 * Pozycja pochodzi z prawdziwego odbiornika GNSS modułu (GPS/GLONASS/Galileo/BeiDou).
 * W tej wersji GNSS jest włączony na stałe (szybkie fixy); optymalizacja poboru
 * prądu (CGNSPWR=0 między raportami, tryb PSM) - patrz kamień M6 w docs/PLAN.md.
 *
 * UWAGA bezpieczeństwo: raport idzie czystym HTTP - token jest widoczny w sieci.
 * Używaj dedykowanego losowego tokenu (nie hasła!). SIM7000E umie TLS 1.2 (HTTPS),
 * ale to świadomie odłożone na później - patrz docs/PLAN.md.
 *
 * Wymagania sprzętowe (szczegóły w docs/SCHEMAT.md):
 *  - ATmega328P na wewnętrznym RC 8 MHz z dzielnikiem /8 = 1 MHz (fabryczne fuse bity)
 *  - UART: PD0(RXD) <- HAT TXD, PD1(TXD) -> HAT RXD (logika 3,3V - zasilaj AVR z 3,3V!)
 *  - PD4 -> pin PWR HAT-a (przełączanie zasilania modułu impulsem)
 *  - PD3: opcjonalna dioda LED statusu (miga po udanym raporcie)
 * --------------------------------------------------------------------------------------------- */

#ifndef F_CPU
#define F_CPU 1000000UL   // 8MHz z dzielnikiem /8 = 1MHz (fabryczne fuses ATmega328P)
#endif

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <util/delay.h>

// *********************************************************************************************
// KONFIGURACJA - dostosuj do swojej karty SIM, operatora i serwera
// *********************************************************************************************
#define SIM_PIN   "1111"       // kod PIN karty SIM (używany tylko gdy karta go wymaga)
#define APN       "internet"   // APN operatora (np. "internet" dla Orange PL)

// Serwer REST - MUSI być http:// (HTTPS to osobny etap - patrz docs/PLAN.md)
#define API_SERVER "http://example.com:8080"   // bazowy adres: protokół + host + port
#define API_PATH   "/api/location"             // ścieżka endpointu
#define API_TOKEN  "twoj-tajny-token"

// Co ile minut wysyłać raport pozycji
#define REPORT_INTERVAL_MIN 10

// Ile sekund czekać na fix GNSS w jednym cyklu (odpytywanie co 5 s)
#define GNSS_FIX_TIMEOUT_SEC 180

// Parametry komunikacji UART
#define BAUD 9600
// FCPU = 1MHz, bit U2X0 daje błąd tylko 0.2% przy 9600 bps
#define MYUBBR ((F_CPU / (BAUD * 8L)) - 1)

// Rozmiar bufora na pojedynczą linię odpowiedzi (linia +CGNSINF ma ~95 znaków)
#define BUFFER_SIZE 120

// Limit czekania na pojedynczy znak z UART: ~5 s przy 1 MHz
#define RX_TIMEOUT_LOOPS 500000UL

// *********************************************************************************************
// Komendy AT i wzorce odpowiedzi (słownik SIM7000E) - w pamięci FLASH (PROGMEM)
// *********************************************************************************************
const char AT[] PROGMEM       = "AT\r\n";
const char ISOK[] PROGMEM     = "OK";
const char ECHO_OFF[] PROGMEM = "ATE0\r\n";

// Utrwalenie prędkości 9600 bps (moduł po starcie robi autodetekcję)
const char SET9600[] PROGMEM  = "AT+IPR=9600\r\n";
const char SAVECNF[] PROGMEM  = "AT&W\r\n";

// Karta SIM
const char SHOW_PIN[] PROGMEM            = "AT+CPIN?\r\n";
const char PIN_IS_READY[] PROGMEM        = "+CPIN: READY";
const char PIN_MUST_BE_ENTERED[] PROGMEM = "+CPIN: SIM PIN";
const char ENTER_PIN[] PROGMEM           = "AT+CPIN=\"" SIM_PIN "\"\r\n";

// Wybór technologii radiowej: auto GSM/LTE + Cat-M i NB-IoT
const char SET_NETMODE[] PROGMEM = "AT+CNMP=2\r\n";   // 2 = automatycznie (LTE i GSM)
const char SET_IOTMODE[] PROGMEM = "AT+CMNB=3\r\n";   // 3 = Cat-M i NB-IoT

// Rejestracja/attach do sieci pakietowej (działa i dla LTE-M, i dla 2G)
const char SHOW_ATTACH[] PROGMEM = "AT+CGATT?\r\n";
const char ISATTACHED[] PROGMEM  = "+CGATT: 1";

// Tryb samolotowy - oszczędzanie baterii gdy brak zasięgu
const char FLIGHT_ON[] PROGMEM  = "AT+CFUN=4\r\n";
const char FLIGHT_OFF[] PROGMEM = "AT+CFUN=1\r\n";

// GNSS (prawdziwy GPS wbudowany w SIM7000E)
const char GNSS_ON[] PROGMEM   = "AT+CGNSPWR=1\r\n";
const char GNSS_INFO[] PROGMEM = "AT+CGNSINF\r\n";
const char ISGNSINF[] PROGMEM  = "+CGNSINF:";

// Połączenie danych warstwy aplikacyjnej (zastępuje SAPBR z SIM800L)
const char NET_ON[] PROGMEM    = "AT+CNACT=1,\"" APN "\"\r\n";
const char NET_OFF[] PROGMEM   = "AT+CNACT=0\r\n";
const char ISPDPACT[] PROGMEM  = "+APP PDP: ACTIVE";

// Klient HTTP SIM7000E (aplikacja "SH")
const char SHCONF_URL[] PROGMEM  = "AT+SHCONF=\"URL\",\"" API_SERVER "\"\r\n";
const char SHCONF_BL[] PROGMEM   = "AT+SHCONF=\"BODYLEN\",1024\r\n";
const char SHCONF_HL[] PROGMEM   = "AT+SHCONF=\"HEADERLEN\",350\r\n";
const char SHCONN[] PROGMEM      = "AT+SHCONN\r\n";
const char SHSTATE[] PROGMEM     = "AT+SHSTATE?\r\n";
const char ISCONNECTED[] PROGMEM = "+SHSTATE: 1";
// początek komendy GET - dalej doklejane są współrzędne z buforów
const char SHREQ_START[] PROGMEM = "AT+SHREQ=\"" API_PATH "?token=" API_TOKEN "&lat=";
const char SHREQ_LON[] PROGMEM   = "&lon=";
const char SHREQ_END[] PROGMEM   = "\",1\r\n";        // 1 = metoda GET
const char ISHTTP200[] PROGMEM   = "+SHREQ: \"GET\",200";
const char SHDISC[] PROGMEM      = "AT+SHDISC\r\n";

// *********************************************************************************************
// Bufory robocze
// *********************************************************************************************
static char response[BUFFER_SIZE];   // ostatnia linia odpowiedzi z modułu
static uint8_t responsePos = 0;
static char latitude[12];            // szerokość geograficzna z CGNSINF
static char longtitude[12];          // długość geograficzna z CGNSINF
static char buf[24];                 // bufor na wzorce kopiowane z PROGMEM do porównań
static uint8_t rxOk;                 // 0 = ostatni odbiór znaku zakończył się timeoutem

// *********************************************************************************************
// UART0 - transmisja odpytywana (polling, bez przerwań)
// Odbiór blokujący z timeoutem ~5s: program czeka na odpowiedzi modemu, ale się nie zawiesi
// *********************************************************************************************
void initUart(void) {
  UCSR0A = (1 << U2X0);                    // podwójna prędkość - mniejszy błąd baud rate
  UBRR0H = (uint8_t)(MYUBBR >> 8);
  UBRR0L = (uint8_t)(MYUBBR);
  UCSR0B = (1 << RXEN0) | (1 << TXEN0);    // RX i TX włączone, BEZ przerwań
  UCSR0C = (0 << USBS0) | (3 << UCSZ00);   // ramka 8N1
}

// Wysłanie pojedynczego znaku (czeka aż rejestr nadawczy będzie wolny)
void sendCharUart(uint8_t charToSend) {
  while (!(UCSR0A & (1 << UDRE0)));
  UDR0 = charToSend;
}

// Odbiór pojedynczego znaku; przy braku danych przez ~5s ustawia rxOk=0 i zwraca 0
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

// Wysłanie łańcucha znaków z RAM
void sendStringUart(const char *stringToSend) {
  while (*stringToSend) {
    sendCharUart(*stringToSend);
    stringToSend++;
  }
}

// Wysłanie łańcucha znaków z pamięci FLASH (PROGMEM)
void sendStringPgm(const char *stringToSend) {
  char c;
  while ((c = pgm_read_byte(stringToSend++)) != 0x00) {
    sendCharUart(c);
  }
}

// *********************************************************************************************
// Opóźnienie w sekundach oparte o pętlę ASM (dokładne przy 1 MHz, nie zajmuje timerów)
// *********************************************************************************************
void delaySec(uint8_t seconds) {
  while (seconds > 0) {
    // 1 000 000 cykli = 1s przy 1MHz
    asm volatile (
      "    ldi  r18, 6"   "\n"
      "    ldi  r19, 19"  "\n"
      "    ldi  r20, 174" "\n"
      "1:  dec  r20"      "\n"
      "    brne 1b"       "\n"
      "    dec  r19"      "\n"
      "    brne 1b"       "\n"
      "    dec  r18"      "\n"
      "    brne 1b"       "\n"
      ::: "r18", "r19", "r20"
    );
    seconds--;
  }
}

// *********************************************************************************************
// Szukanie podciągu 'sub' w buforze 'str' (odpowiednik strstr, ale odporny na śmieci w buforze)
// *********************************************************************************************
uint8_t isInBuffer(char *str, char *sub) {
  uint8_t i, j = 0, k;
  for (i = 0; i < BUFFER_SIZE; i++) {
    if (str[i] == sub[j]) {
      for (k = i, j = 0; str[k] && sub[j]; j++, k++)
        if (str[k] != sub[j]) break;
      if (j == strlen(sub)) return 1;
    }
  }
  return 0;
}

// Wygodne opakowanie: czy ostatnia odpowiedź zawiera wzorzec z PROGMEM?
uint8_t responseContains_P(const char *pattern, uint8_t patternSize) {
  memcpy_P(buf, pattern, patternSize);
  return isInBuffer(response, buf);
}

// *********************************************************************************************
// Odczyt jednej niepustej linii z UART do bufora 'response' (pomija puste CR/LF)
// Zwraca 0 gdy modem zamilkł na ~5s (timeout)
// *********************************************************************************************
uint8_t readLine(void) {
  uint8_t char1;
  responsePos = 0;
  rxOk = 1;
  while (1) {
    char1 = receiveCharUart();
    if (!rxOk) return 0;
    if (char1 != 0x0a && char1 != 0x0d) {
      if (responsePos < BUFFER_SIZE - 1) {
        response[responsePos] = char1;
        responsePos++;
      }
    } else if (responsePos > 0) {
      response[responsePos] = '\0';
      return 1;
    }
  }
}

// *********************************************************************************************
// Czytaj kolejne linie aż któraś będzie zawierać wzorzec z PROGMEM.
// maxTimeouts ogranicza łączny czas czekania (każdy timeout to ~5s ciszy na UART).
// *********************************************************************************************
uint8_t waitFor_P(const char *pattern, uint8_t patternSize, uint8_t maxTimeouts) {
  uint8_t timeouts = 0;
  while (timeouts < maxTimeouts) {
    if (readLine()) {
      memcpy_P(buf, pattern, patternSize);
      if (isInBuffer(response, buf)) return 1;
    } else {
      timeouts++;
    }
  }
  return 0;
}

// *********************************************************************************************
// Parsowanie linii +CGNSINF: <run>,<fix>,<utc>,<szerokość>,<długość>,<wysokość>,...
// (uwaga: kolejność szerokość-długość, odwrotnie niż w CIPGSMLOC z SIM800L)
// Zwraca 1 i wypełnia bufory latitude/longtitude tylko gdy jest fix.
// *********************************************************************************************
uint8_t parseGnsInf(void) {
  char *p = strchr(response, ':');
  uint8_t field = 0;    // 0=run, 1=fix, 2=utc, 3=szerokość, 4=długość
  uint8_t pos = 0;
  uint8_t fix = 0;

  if (p == 0) return 0;
  p++;
  while (*p == ' ') p++;

  latitude[0] = '\0';
  longtitude[0] = '\0';

  for (; *p; p++) {
    if (*p == ',') {
      if (field == 3) latitude[pos] = '\0';
      if (field == 4) { longtitude[pos] = '\0'; break; }   // mamy wszystko
      field++;
      pos = 0;
      continue;
    }
    if (field == 1 && *p == '1') fix = 1;
    if (field == 3 && pos < sizeof(latitude) - 1)   latitude[pos++] = *p;
    if (field == 4 && pos < sizeof(longtitude) - 1) longtitude[pos++] = *p;
  }

  return (fix && latitude[0] != '\0' && longtitude[0] != '\0');
}

// *********************************************************************************************
// Dioda statusu na PD3 - 3 szybkie mignięcia po udanym raporcie
// *********************************************************************************************
void blinkStatus(void) {
  uint8_t i;
  for (i = 0; i < 3; i++) {
    PORTD |= (1 << PD3);
    _delay_ms(150);
    PORTD &= ~(1 << PD3);
    _delay_ms(150);
  }
}

// *********************************************************************************************
// Zarządzanie zasilaniem modułu przez pin PWR HAT-a (PD4)
// Impuls WYSOKI ~2s przełącza zasilanie modułu (włącza wyłączony / wyłącza włączony)
// *********************************************************************************************
void togglePower(void) {
  PORTD |= (1 << PD4);
  delaySec(2);
  PORTD &= ~(1 << PD4);
  delaySec(15);   // czas na start modułu
}

// *********************************************************************************************
// Inicjalizacja modułu SIM7000E
// *********************************************************************************************

// Czekaj aż moduł odpowie OK na AT; jeśli długo milczy - spróbuj włączyć go pinem PWR
uint8_t checkAt(void) {
  uint8_t silent = 0;
  while (1) {
    sendStringPgm(AT);
    if (waitFor_P(ISOK, sizeof(ISOK), 1)) break;
    silent++;
    if (silent >= 4) {       // ~20s ciszy: moduł pewnie wyłączony - impuls PWRKEY
      togglePower();
      silent = 0;
    }
  }
  delaySec(1);
  sendStringPgm(ECHO_OFF);   // wyłącz echo komend - upraszcza parsowanie odpowiedzi
  delaySec(1);
  return 1;
}

// Sprawdź status PIN karty SIM, w razie potrzeby podaj kod PIN
uint8_t checkPin(void) {
  while (1) {
    sendStringPgm(SHOW_PIN);
    if (readLine()) {
      if (responseContains_P(PIN_IS_READY, sizeof(PIN_IS_READY))) return 1;
      if (responseContains_P(PIN_MUST_BE_ENTERED, sizeof(PIN_MUST_BE_ENTERED))) {
        sendStringPgm(ENTER_PIN);
        delaySec(5);
      }
    }
    delaySec(2);
  }
}

// Czekaj na attach do sieci pakietowej (LTE-M/NB-IoT/2G); po ~5 minutach bez sieci:
// tryb samolotowy i sen na 30 minut (nie drenuj baterii np. w podziemnym garażu)
uint8_t checkAttach(void) {
  uint8_t attempts = 0, m;
  while (1) {
    sendStringPgm(SHOW_ATTACH);
    if (readLine()) {
      if (responseContains_P(ISATTACHED, sizeof(ISATTACHED))) return 1;
    }
    delaySec(15);
    attempts++;
    if (attempts >= 20) {
      sendStringPgm(FLIGHT_ON);
      for (m = 0; m < 30; m++) {
        delaySec(60);
      }
      sendStringPgm(FLIGHT_OFF);
      delaySec(10);
      attempts = 0;
    }
  }
}

// *********************************************************************************************
// Pobranie pozycji z GNSS: odpytuj AT+CGNSINF co 5s aż będzie fix (max GNSS_FIX_TIMEOUT_SEC)
// *********************************************************************************************
uint8_t getGnssFix(void) {
  uint8_t tries = GNSS_FIX_TIMEOUT_SEC / 5;
  while (tries > 0) {
    sendStringPgm(GNSS_INFO);
    if (waitFor_P(ISGNSINF, sizeof(ISGNSINF), 1)) {
      if (parseGnsInf()) return 1;   // jest fix - współrzędne w buforach
    }
    delaySec(5);
    tries--;
  }
  return 0;   // brak fixu w tym cyklu (np. auto w garażu podziemnym)
}

// *********************************************************************************************
// Wysłanie raportu HTTP GET przez klienta "SH" modułu. Zwraca 1 gdy serwer odpowiedział 200.
// *********************************************************************************************
uint8_t sendReport(void) {
  uint8_t ok = 0;

  // aktywuj połączenie danych i czekaj na potwierdzenie PDP
  sendStringPgm(NET_ON);
  if (waitFor_P(ISPDPACT, sizeof(ISPDPACT), 4) == 0) {
    sendStringPgm(NET_OFF);
    delaySec(2);
    return 0;
  }
  delaySec(1);

  // konfiguracja klienta HTTP i połączenie z serwerem
  sendStringPgm(SHCONF_URL);
  delaySec(1);
  sendStringPgm(SHCONF_BL);
  delaySec(1);
  sendStringPgm(SHCONF_HL);
  delaySec(1);
  sendStringPgm(SHCONN);       // nawiązanie połączenia TCP - może potrwać kilka sekund
  delaySec(5);
  sendStringPgm(SHSTATE);
  if (waitFor_P(ISCONNECTED, sizeof(ISCONNECTED), 2)) {
    // GET /api/location?token=...&lat=<szer>&lon=<dług>
    sendStringPgm(SHREQ_START);
    sendStringUart(latitude);
    sendStringPgm(SHREQ_LON);
    sendStringUart(longtitude);
    sendStringPgm(SHREQ_END);
    // czekaj na URC +SHREQ: "GET",200,<długość> - do ~40s
    if (waitFor_P(ISHTTP200, sizeof(ISHTTP200), 8)) {
      ok = 1;
    }
    sendStringPgm(SHDISC);     // rozłącz klienta HTTP
    delaySec(1);
  }

  // dezaktywuj połączenie danych
  sendStringPgm(NET_OFF);
  delaySec(2);
  return ok;
}

// *********************************************************************************************
//
//                                     PROGRAM GŁÓWNY
//
// *********************************************************************************************
int main(void) {

  uint8_t m;

  initUart();

  // PD3: dioda statusu, PD4: sterowanie pinem PWR HAT-a (spoczynkowo stan niski)
  DDRD |= (1 << PD3) | (1 << PD4);
  PORTD &= ~((1 << PD3) | (1 << PD4));

  // czas na start HAT-a po podaniu zasilania
  delaySec(5);

  // nawiąż komunikację AT (w razie potrzeby włączy moduł pinem PWR)
  checkAt();

  // utrwal prędkość 9600 bps
  sendStringPgm(SET9600);
  delaySec(1);
  sendStringPgm(SAVECNF);
  delaySec(2);

  // PIN karty, tryb sieci (auto LTE-M/NB-IoT/GSM), GNSS włączony na stałe
  checkPin();
  sendStringPgm(SET_NETMODE);
  delaySec(2);
  sendStringPgm(SET_IOTMODE);
  delaySec(2);
  sendStringPgm(GNSS_ON);
  delaySec(2);

  // czekaj na zalogowanie do sieci pakietowej
  checkAttach();

  // pętla główna: raport pozycji co REPORT_INTERVAL_MIN minut
  while (1) {

    if (getGnssFix()) {
      // jest pozycja - upewnij się że sieć jest, wyślij raport
      checkAttach();
      if (sendReport()) {
        blinkStatus();     // serwer potwierdził (HTTP 200)
      }
    }
    // brak fixu = pomiń ten cykl (spróbujemy za REPORT_INTERVAL_MIN minut)

    for (m = 0; m < REPORT_INTERVAL_MIN; m++) {
      delaySec(60);
    }
  }
}
