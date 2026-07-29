/* ---------------------------------------------------------------------------------------------
 * GPS tracker na ATTINY2313 + SIM800L - wersja z raportowaniem HTTP (REST)
 * Inspirowany projektem https://github.com/mcore1976/gpstracker (main3b.c),
 * ale zamiast dzwonienia i SMS: cykliczny raport pozycji na serwer web.
 *
 * Zasada działania:
 *  1. Po starcie konfiguruje SIM800L (stała prędkość 9600, echo off, PIN, rejestracja 2G, APN).
 *  2. Co REPORT_INTERVAL_MIN minut:
 *     - budzi SIM800L i otwiera kontekst GPRS (AT+SAPBR),
 *     - pyta o pozycję najbliższej stacji bazowej BTS (AT+CIPGSMLOC),
 *     - wysyła HTTP GET na serwer:  API_URL?token=API_TOKEN&lat=<szer>&lon=<dług>
 *     - po odpowiedzi HTTP 200 miga diodą na PD3,
 *     - zamyka GPRS, usypia SIM800L (AT+CSCLK=2) i czeka do następnego raportu.
 *  3. Przy braku zasięgu 2G: tryb samolotowy na 30 min (oszczędzanie baterii w garażu).
 *
 * Dokładność pozycji = pozycja stacji bazowej GSM (SIM800L nie ma GPS).
 *
 * UWAGA bezpieczeństwo: SIM800L nie obsługuje współczesnego TLS - raport idzie czystym
 * HTTP, więc token jest widoczny dla operatora/sieci. Używaj dedykowanego tokenu
 * (nie hasła!) i waliduj go po stronie serwera.
 *
 * Wymagania sprzętowe (szczegóły w docs/SCHEMAT.md):
 *  - zegar 1 MHz (wewnętrzny RC 8 MHz z dzielnikiem /8 - fabryczne fuse bity ATtiny2313)
 *  - UART: PD0(RXD) <- SIM800L TXD, PD1(TXD) -> SIM800L RXD
 *  - PD3: opcjonalna dioda LED statusu (miga po udanym raporcie)
 *  - SIM800L musi mieć ustawione na stałe 9600 bps: AT+IPR=9600 + AT&W
 *    (program też to ustawia przy każdym starcie)
 * --------------------------------------------------------------------------------------------- */

#ifndef F_CPU
#define F_CPU 1000000UL   // 8MHz z dzielnikiem /8 = 1MHz (fabryczne fuses ATtiny2313)
#endif

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <util/delay.h>

// *********************************************************************************************
// KONFIGURACJA - dostosuj do swojej karty SIM, operatora i serwera
// *********************************************************************************************
#define SIM_PIN   "1111"       // kod PIN karty SIM (używany tylko gdy karta go wymaga)
#define GPRS_APN  "internet"   // APN operatora (np. "internet" dla Orange/Play/Plus/T-Mobile PL)
#define GPRS_USER "internet"   // użytkownik APN (w PL zwykle pusty lub "internet")
#define GPRS_PASS "internet"   // hasło APN

// Adres endpointu REST - MUSI być http:// (SIM800L nie obsługuje nowoczesnego TLS!)
// Do URL doklejane jest: ?token=<API_TOKEN>&lat=<szerokość>&lon=<długość>
#define API_URL   "http://example.com:8080/api/location"
#define API_TOKEN "twoj-tajny-token"

// Co ile minut wysyłać raport pozycji (im rzadziej, tym dłużej działa bateria)
#define REPORT_INTERVAL_MIN 10

// Parametry komunikacji UART - jak w attiny-rpi-2way-com.c
#define BAUD 9600
// FCPU = 1MHz, bit U2X daje błąd tylko 0.2% przy 9600 bps
#define MYUBBR ((F_CPU / (BAUD * 8L)) - 1)

// Rozmiar bufora na pojedynczą linię odpowiedzi z SIM800L
#define BUFFER_SIZE 40

// Limit czekania na pojedynczy znak z UART: ~5 s przy 1 MHz
// (pętla odpytująca RXC; dzięki temu program nie zawiesza się gdy modem milczy)
#define RX_TIMEOUT_LOOPS 500000UL

// *********************************************************************************************
// Komendy AT i wzorce odpowiedzi - trzymane w pamięci FLASH (PROGMEM), bo RAM to tylko 128B
// *********************************************************************************************
const char AT[] PROGMEM              = "AT\r\n";
const char ISOK[] PROGMEM            = "OK";
const char ISREG1[] PROGMEM          = "+CREG: 0,1";      // zarejestrowany w sieci macierzystej
const char ISREG2[] PROGMEM          = "+CREG: 0,5";      // zarejestrowany w roamingu
const char SHOW_REGISTRATION[] PROGMEM = "AT+CREG?\r\n";
const char DISREGURC[] PROGMEM       = "AT+CREG=0\r\n";   // wyłącz raportowanie utraty zasięgu
const char PIN_IS_READY[] PROGMEM    = "+CPIN: READY";
const char PIN_MUST_BE_ENTERED[] PROGMEM = "+CPIN: SIM PIN";
const char SHOW_PIN[] PROGMEM        = "AT+CPIN?\r\n";
const char ECHO_OFF[] PROGMEM        = "ATE0\r\n";
const char ENTER_PIN[] PROGMEM       = "AT+CPIN=\"" SIM_PIN "\"\r\n";

// Tryb samolotowy - oszczędzanie baterii gdy brak zasięgu (np. podziemny garaż)
const char FLIGHT_ON[] PROGMEM       = "AT+CFUN=4\r\n";
const char FLIGHT_OFF[] PROGMEM      = "AT+CFUN=1\r\n";

// Tryb uśpienia SIM800L (CSCLK=2: moduł śpi, budzi go aktywność na UART)
const char SLEEP_ON[] PROGMEM        = "AT+CSCLK=2\r\n";
const char SLEEP_OFF[] PROGMEM       = "AT+CSCLK=0\r\n";

// Utrwalenie prędkości 9600 bps w konfiguracji SIM800L
const char SET9600[] PROGMEM         = "AT+IPR=9600\r\n";
const char SAVECNF[] PROGMEM         = "AT&W\r\n";

// Konfiguracja GPRS (kontekst IP potrzebny do CIPGSMLOC i HTTP)
const char SAPBR1[] PROGMEM = "AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"\r\n";
const char SAPBR2[] PROGMEM = "AT+SAPBR=3,1,\"APN\",\"" GPRS_APN "\"\r\n";
const char SAPBR3[] PROGMEM = "AT+SAPBR=3,1,\"USER\",\"" GPRS_USER "\"\r\n";
const char SAPBR4[] PROGMEM = "AT+SAPBR=3,1,\"PWD\",\"" GPRS_PASS "\"\r\n";
const char SAPBROPEN[] PROGMEM  = "AT+SAPBR=1,1\r\n";   // otwórz kontekst IP
const char SAPBRQUERY[] PROGMEM = "AT+SAPBR=2,1\r\n";   // sprawdź kontekst IP
const char SAPBRCLOSE[] PROGMEM = "AT+SAPBR=0,1\r\n";   // zamknij kontekst IP
const char SAPBRSUCC[] PROGMEM  = "+SAPBR: 1,1";        // odpowiedź gdy kontekst otwarty
const char CHECKGPS[] PROGMEM   = "AT+CIPGSMLOC=1,1\r\n"; // pozycja najbliższego BTS

// Wbudowany klient HTTP modułu SIM800L
const char HTTPINIT[] PROGMEM     = "AT+HTTPINIT\r\n";
const char HTTPPARA_CID[] PROGMEM = "AT+HTTPPARA=\"CID\",1\r\n";
// początek komendy z URL - dalej doklejane są współrzędne z buforów
const char HTTPURL[] PROGMEM      = "AT+HTTPPARA=\"URL\",\"" API_URL "?token=" API_TOKEN "&lat=";
const char URL_LON[] PROGMEM      = "&lon=";
const char URL_END[] PROGMEM      = "\"\r\n";
const char HTTPACTION[] PROGMEM   = "AT+HTTPACTION=0\r\n";  // 0 = metoda GET
const char HTTPTERM[] PROGMEM     = "AT+HTTPTERM\r\n";
const char ISHTTPOK[] PROGMEM     = "+HTTPACTION: 0,200";   // serwer przyjął raport

// *********************************************************************************************
// Bufory robocze (RAM ATtiny2313 to tylko 128 bajtów - stąd oszczędne rozmiary)
// *********************************************************************************************
static char response[BUFFER_SIZE];   // ostatnia linia odpowiedzi z SIM800L
static uint8_t responsePos = 0;
static char latitude[10];            // szerokość geograficzna z CIPGSMLOC
static char longtitude[10];          // długość geograficzna z CIPGSMLOC
static char buf[20];                 // bufor na wzorce kopiowane z PROGMEM do porównań
static uint8_t rxOk;                 // 0 = ostatni odbiór znaku zakończył się timeoutem

// *********************************************************************************************
// UART - transmisja odpytywana (polling, bez przerwań)
// Odbiór blokujący z timeoutem ~5s: program czeka na odpowiedzi modemu, ale się nie zawiesi
// *********************************************************************************************
void initUart(void) {
  UCSRA = (1 << U2X);                    // podwójna prędkość - mniejszy błąd baud rate
  UBRRH = (uint8_t)(MYUBBR >> 8);
  UBRRL = (uint8_t)(MYUBBR);
  UCSRB = (1 << RXEN) | (1 << TXEN);     // RX i TX włączone, BEZ przerwań
  UCSRC = (0 << USBS) | (3 << UCSZ0);    // ramka 8N1
}

// Wysłanie pojedynczego znaku (czeka aż rejestr nadawczy będzie wolny)
void sendCharUart(uint8_t charToSend) {
  while (!(UCSRA & (1 << UDRE)));
  UDR = charToSend;
}

// Odbiór pojedynczego znaku; przy braku danych przez ~5s ustawia rxOk=0 i zwraca 0
uint8_t receiveCharUart(void) {
  uint32_t i = 0;
  while (!(UCSRA & (1 << RXC))) {
    if (++i > RX_TIMEOUT_LOOPS) {
      rxOk = 0;
      return 0;
    }
  }
  return UDR;
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
    // 1 000 000 cykli = 1s przy 1MHz (wygenerowane przez kalkulator pętli opóźniających)
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
        if (str[k] != sub[j]) break;     // różnica - porównuj od następnego znaku
      if (j == strlen(sub)) return 1;    // znaleziono cały podciąg
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
      // zwykły znak - dopisz do bufora (z ochroną przed przepełnieniem)
      if (responsePos < BUFFER_SIZE - 1) {
        response[responsePos] = char1;
        responsePos++;
      }
    } else if (responsePos > 0) {
      // CR lub LF po jakichś znakach = koniec linii (puste CR/LF pomijamy)
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
// Parsowanie odpowiedzi AT+CIPGSMLOC: +CIPGSMLOC: 0,<długość>,<szerokość>,<data>,<czas>
// Współrzędne trafiają do 'longtitude' i 'latitude'. Zwraca 0 przy błędzie/timeoucie
// (np. odpowiedź "+CIPGSMLOC: 601" bez współrzędnych).
// *********************************************************************************************
uint8_t readCellGps(void) {
  uint8_t char1;
  uint8_t pos = 0;
  rxOk = 1;

  // czekaj na pierwszy przecinek (po kodzie statusu "0")
  do {
    char1 = receiveCharUart();
    if (!rxOk) return 0;
  } while (char1 != ',');

  // kopiuj DŁUGOŚĆ geograficzną do przecinka
  do {
    char1 = receiveCharUart();
    if (!rxOk) return 0;
    if (pos < sizeof(longtitude) - 1) longtitude[pos++] = char1;
  } while (char1 != ',');
  longtitude[pos - 1] = '\0';
  pos = 0;

  // kopiuj SZEROKOŚĆ geograficzną do przecinka
  do {
    char1 = receiveCharUart();
    if (!rxOk) return 0;
    if (pos < sizeof(latitude) - 1) latitude[pos++] = char1;
  } while (char1 != ',');
  latitude[pos - 1] = '\0';

  // resztę linii (data/czas) pomijamy - znacznik czasu nadaje serwer przy odbiorze;
  // doczytujemy tylko do końca linii, żeby opróżnić bufor odbiorczy
  do {
    char1 = receiveCharUart();
    if (!rxOk) return 0;
  } while (char1 != 0x0a && char1 != 0x0d);

  return 1;
}

// *********************************************************************************************
// Dioda statusu na PD3 - 3 szybkie mignięcia po udanym raporcie (pomocne przy debugowaniu)
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
// Inicjalizacja SIM800L
// *********************************************************************************************

// Czekaj aż moduł odpowie OK na AT (SIM800L po starcie robi autodetekcję prędkości,
// więc AT trzeba powtarzać aż moduł złapie 9600 bps)
uint8_t checkAt(void) {
  do {
    sendStringPgm(AT);
  } while (waitFor_P(ISOK, sizeof(ISOK), 1) == 0);
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

// Czekaj na rejestrację w sieci 2G; po ~5 minutach bez sieci: tryb samolotowy
// i sen na 30 minut (nie drenuj baterii np. w podziemnym garażu)
uint8_t checkRegistration(void) {
  uint8_t attempts = 0, m;
  while (1) {
    sendStringPgm(SHOW_REGISTRATION);
    if (readLine()) {
      if (responseContains_P(ISREG1, sizeof(ISREG1))) return 1;
      if (responseContains_P(ISREG2, sizeof(ISREG2))) return 1;
    }
    delaySec(15);
    attempts++;
    if (attempts >= 20) {
      // ~5 minut bez sieci: wyłącz radio i uśpij moduł na 30 minut
      sendStringPgm(FLIGHT_ON);
      delaySec(1);
      sendStringPgm(SLEEP_ON);
      for (m = 0; m < 30; m++) {
        delaySec(60);
      }
      // obudź moduł, włącz radio i szukaj sieci od nowa
      sendStringPgm(AT);
      delaySec(1);
      sendStringPgm(SLEEP_OFF);
      delaySec(1);
      sendStringPgm(FLIGHT_OFF);
      delaySec(5);
      attempts = 0;
    }
  }
}

// Skonfiguruj parametry GPRS/APN (bez sprawdzania błędów - unikamy zakleszczeń)
uint8_t provisionGprs(void) {
  delaySec(2);
  sendStringPgm(SAPBR1);
  delaySec(1);
  sendStringPgm(SAPBR2);
  delaySec(1);
  sendStringPgm(SAPBR3);
  delaySec(1);
  sendStringPgm(SAPBR4);
  delaySec(1);
  return 1;
}

// *********************************************************************************************
//
//                                     PROGRAM GŁÓWNY
//
// *********************************************************************************************
int main(void) {

  uint8_t initialized, attempt, m;

  initUart();

  // PD3 jako wyjście - opcjonalna dioda statusu
  DDRD |= (1 << PD3);
  PORTD &= ~(1 << PD3);

  // 10 sekund na bezpieczny start SIM800L
  delaySec(10);

  // nawiąż komunikację AT i utrwal konfigurację 9600 bps
  checkAt();
  sendStringPgm(SET9600);
  delaySec(2);
  sendStringPgm(DISREGURC);
  delaySec(2);
  sendStringPgm(SAVECNF);
  delaySec(3);

  // PIN karty SIM, rejestracja w sieci i parametry APN
  checkPin();
  checkRegistration();
  provisionGprs();

  // pętla główna: raport pozycji co REPORT_INTERVAL_MIN minut
  while (1) {

    // otwórz kontekst GPRS - do 3 prób
    initialized = 0;
    attempt = 0;
    do {
      sendStringPgm(SAPBRCLOSE);   // zamknij kontekst na wypadek błędu
      delaySec(2);
      sendStringPgm(SAPBROPEN);    // otwórz kontekst IP (GPRS attach)
      delaySec(5);
      sendStringPgm(SAPBRQUERY);   // sprawdź czy dostaliśmy adres IP
      if (waitFor_P(SAPBRSUCC, sizeof(SAPBRSUCC), 1)) initialized = 1;
      attempt++;
    } while ((attempt < 3) && (initialized == 0));

    if (initialized == 1) {
      // GPRS działa - pobierz pozycję najbliższego BTS
      delaySec(1);
      sendStringPgm(CHECKGPS);
      if (readCellGps()) {
        // wyślij raport HTTP GET: API_URL?token=...&lat=<szer>&lon=<dług>
        sendStringPgm(HTTPINIT);
        delaySec(2);
        sendStringPgm(HTTPPARA_CID);
        delaySec(1);
        sendStringPgm(HTTPURL);        // AT+HTTPPARA="URL","http://...?token=...&lat=
        sendStringUart(latitude);
        sendStringPgm(URL_LON);        // &lon=
        sendStringUart(longtitude);
        sendStringPgm(URL_END);        // "CRLF
        delaySec(1);
        sendStringPgm(HTTPACTION);     // wykonaj GET
        // czekaj na URC +HTTPACTION: 0,<kod>,<długość> - do ~40s (8 timeoutów po 5s)
        if (waitFor_P(ISHTTPOK, sizeof(ISHTTPOK), 8)) {
          blinkStatus();               // serwer odpowiedział 200 - raport dostarczony
        }
        sendStringPgm(HTTPTERM);
        delaySec(1);
      }
      // zamknij kontekst IP
      sendStringPgm(SAPBRCLOSE);
      delaySec(2);
    } else {
      // GPRS nie wstał po 3 próbach - moduł mógł się zrestartować (np. spadek napięcia):
      // przejdź pełną rekonfigurację od początku
      checkAt();
      checkPin();
      checkRegistration();
      provisionGprs();
    }

    // uśpij SIM800L i czekaj do następnego raportu
    sendStringPgm(SLEEP_ON);
    for (m = 0; m < REPORT_INTERVAL_MIN; m++) {
      delaySec(60);
    }
    // obudź moduł (dowolne znaki na UART + wyłączenie trybu uśpienia)
    sendStringPgm(AT);
    delaySec(1);
    sendStringPgm(SLEEP_OFF);
    delaySec(1);
  }
}
