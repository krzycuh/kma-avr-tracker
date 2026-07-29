/* ---------------------------------------------------------------------------------------------
 * GPS tracker na ATTINY2313 + SIM800L
 * Port programu main3b.c z projektu https://github.com/mcore1976/gpstracker
 * dostosowany do stylu i środowiska tego repozytorium (avr-build + avrdude linuxgpio na RPi).
 *
 * Zasada działania:
 *  1. Po starcie konfiguruje SIM800L (stała prędkość 9600, echo off, PIN, rejestracja w sieci 2G).
 *  2. Usypia SIM800L (AT+CSCLK=2) i czeka aż pin RI/RING (podłączony do PD2) opadnie do stanu
 *     niskiego - to oznacza przychodzące połączenie głosowe.
 *  3. Po "zadzwonieniu" na tracker: odczytuje numer dzwoniącego (URC +CLIP), odrzuca połączenie,
 *     łączy się z GPRS, pyta o pozycję najbliższej stacji bazowej BTS (AT+CIPGSMLOC)
 *     i odsyła SMS z linkiem do Google Maps na numer dzwoniącego.
 *  4. Wraca do snu. Co 30 minut budzi SIM800L i sprawdza zasięg 2G
 *     (przy braku zasięgu: tryb samolotowy na 30 min - oszczędzanie baterii).
 *
 * Dokładność pozycji = pozycja stacji bazowej GSM (SIM800L nie ma GPS).
 *
 * Wymagania sprzętowe (szczegóły w docs/SCHEMAT.md):
 *  - zegar 1 MHz (wewnętrzny RC 8 MHz z dzielnikiem /8 - fabryczne fuse bity ATtiny2313)
 *  - UART: PD0(RXD) <- SIM800L TXD, PD1(TXD) -> SIM800L RXD
 *  - PD2 <- SIM800L RI/RING (w tej wersji na PD2 NIE może być diody LED!)
 *  - SIM800L musi mieć ustawione na stałe 9600 bps: AT+IPR=9600 + AT&W
 *    (program też to ustawia przy każdym starcie)
 * --------------------------------------------------------------------------------------------- */

#ifndef F_CPU
#define F_CPU 1000000UL   // 8MHz z dzielnikiem /8 = 1MHz (fabryczne fuses ATtiny2313)
#endif

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>

// *********************************************************************************************
// KONFIGURACJA - dostosuj do swojej karty SIM i operatora
// *********************************************************************************************
#define SIM_PIN   "1111"       // kod PIN karty SIM (używany tylko gdy karta go wymaga)
#define GPRS_APN  "internet"   // APN operatora (np. "internet" dla Orange/Play/Plus/T-Mobile PL)
#define GPRS_USER "internet"   // użytkownik APN (w PL zwykle pusty lub "internet")
#define GPRS_PASS "internet"   // hasło APN

// Parametry komunikacji UART - jak w attiny-rpi-2way-com.c
#define BAUD 9600
// FCPU = 1MHz, bit U2X daje błąd tylko 0.2% przy 9600 bps
#define MYUBBR ((F_CPU / (BAUD * 8L)) - 1)

// Rozmiar bufora na pojedynczą linię odpowiedzi z SIM800L
#define BUFFER_SIZE 40

// *********************************************************************************************
// Komendy AT i wzorce odpowiedzi - trzymane w pamięci FLASH (PROGMEM), bo RAM to tylko 128B
// *********************************************************************************************
const char AT[] PROGMEM              = "AT\r\n";
const char ISOK[] PROGMEM            = "OK";
const char ISRING[] PROGMEM          = "RING";
const char ISREG1[] PROGMEM          = "+CREG: 0,1";      // zarejestrowany w sieci macierzystej
const char ISREG2[] PROGMEM          = "+CREG: 0,5";      // zarejestrowany w roamingu
const char SHOW_REGISTRATION[] PROGMEM = "AT+CREG?\r\n";
const char DISREGURC[] PROGMEM       = "AT+CREG=0\r\n";   // wyłącz raportowanie utraty zasięgu
const char PIN_IS_READY[] PROGMEM    = "+CPIN: READY";
const char PIN_MUST_BE_ENTERED[] PROGMEM = "+CPIN: SIM PIN";
const char SHOW_PIN[] PROGMEM        = "AT+CPIN?\r\n";
const char ECHO_OFF[] PROGMEM        = "ATE0\r\n";
const char ENTER_PIN[] PROGMEM       = "AT+CPIN=\"" SIM_PIN "\"\r\n";
const char HANGUP[] PROGMEM          = "ATH\r\n";
const char SMS_TEXTMODE[] PROGMEM    = "AT+CMGF=1\r\n";
const char SMS_SEND[] PROGMEM        = "AT+CMGS=\"";
const char DELSMS[] PROGMEM          = "AT+CMGDA=\"DEL ALL\"\r\n";  // czyść pamięć SMS
const char QUOTE_CRLF[] PROGMEM      = "\"\r\n";
const char CLIP_ON[] PROGMEM         = "AT+CLIP=1\r\n";   // prezentacja numeru dzwoniącego

// Tryb samolotowy - oszczędzanie baterii gdy brak zasięgu (np. podziemny garaż)
const char FLIGHT_ON[] PROGMEM       = "AT+CFUN=4\r\n";
const char FLIGHT_OFF[] PROGMEM      = "AT+CFUN=1\r\n";

// Tryb uśpienia SIM800L (CSCLK=2: moduł śpi, budzi go aktywność sieci)
const char SLEEP_ON[] PROGMEM        = "AT+CSCLK=2\r\n";
const char SLEEP_OFF[] PROGMEM       = "AT+CSCLK=0\r\n";

// Utrwalenie prędkości 9600 bps w konfiguracji SIM800L
const char SET9600[] PROGMEM         = "AT+IPR=9600\r\n";
const char SAVECNF[] PROGMEM         = "AT&W\r\n";

// Fragmenty treści SMS z pozycją
const char GOOGLELOC1[] PROGMEM      = "\r\n http://maps.google.com/maps?q=";
const char COMMA[] PROGMEM           = ",";
const char CRLF[] PROGMEM            = "\r\n";
const char LONG[] PROGMEM            = " UTC\n LONG=";
const char LATT[] PROGMEM            = " LATT=";

// Konfiguracja GPRS (kontekst IP potrzebny do zapytania o pozycję BTS)
const char SAPBR1[] PROGMEM = "AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"\r\n";
const char SAPBR2[] PROGMEM = "AT+SAPBR=3,1,\"APN\",\"" GPRS_APN "\"\r\n";
const char SAPBR3[] PROGMEM = "AT+SAPBR=3,1,\"USER\",\"" GPRS_USER "\"\r\n";
const char SAPBR4[] PROGMEM = "AT+SAPBR=3,1,\"PWD\",\"" GPRS_PASS "\"\r\n";
const char SAPBROPEN[] PROGMEM  = "AT+SAPBR=1,1\r\n";   // otwórz kontekst IP
const char SAPBRQUERY[] PROGMEM = "AT+SAPBR=2,1\r\n";   // sprawdź kontekst IP
const char SAPBRCLOSE[] PROGMEM = "AT+SAPBR=0,1\r\n";   // zamknij kontekst IP
const char SAPBRSUCC[] PROGMEM  = "+SAPBR: 1,1";        // odpowiedź gdy kontekst otwarty
const char CHECKGPS[] PROGMEM   = "AT+CIPGSMLOC=1,1\r\n"; // pozycja najbliższego BTS

// *********************************************************************************************
// Bufory robocze (RAM ATtiny2313 to tylko 128 bajtów - stąd oszczędne rozmiary)
// *********************************************************************************************
static char response[BUFFER_SIZE];   // ostatnia linia odpowiedzi z SIM800L
static uint8_t responsePos = 0;
static char phonenumber[15];         // numer dzwoniącego odczytany z +CLIP
static char latitude[10];            // szerokość geograficzna z CIPGSMLOC
static char longtitude[10];          // długość geograficzna z CIPGSMLOC
static char buf[20];                 // bufor na wzorce kopiowane z PROGMEM do porównań

// *********************************************************************************************
// UART - transmisja odpytywana (polling, bez przerwań), jak w main3b.c
// Odbiór blokujący jest tu zaletą: program i tak czeka na odpowiedzi modemu
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

// Odbiór pojedynczego znaku (blokuje aż coś przyjdzie)
uint8_t receiveCharUart(void) {
  while (!(UCSRA & (1 << RXC)));
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
// *********************************************************************************************
uint8_t readLine(void) {
  uint8_t char1;
  uint8_t wholeline = 0;
  responsePos = 0;

  do {
    char1 = receiveCharUart();
    if (char1 != 0x0a && char1 != 0x0d) {
      // zwykły znak - dopisz do bufora (z ochroną przed przepełnieniem)
      if (responsePos < BUFFER_SIZE - 1) {
        response[responsePos] = char1;
        responsePos++;
      }
    } else {
      // CR lub LF: jeśli mamy już jakieś znaki, to koniec linii
      if (responsePos > 0) {
        response[responsePos] = '\0';
        responsePos = 0;
        wholeline = 1;
      }
      // puste CR/LF na początku - pomiń i czekaj dalej
    }
  } while (wholeline == 0);
  return 1;
}

// *********************************************************************************************
// Parsowanie odpowiedzi AT+CIPGSMLOC: +CIPGSMLOC: 0,<długość>,<szerokość>,<data>,<czas>
// Wynik trafia do buforów 'longtitude', 'latitude' oraz 'response' (data i czas UTC)
// *********************************************************************************************
uint8_t readCellGps(void) {
  uint8_t char1;
  uint8_t pos = 0;

  // czekaj na pierwszy przecinek (po kodzie statusu "0")
  do { char1 = receiveCharUart(); } while (char1 != ',');

  // kopiuj DŁUGOŚĆ geograficzną do przecinka
  do {
    char1 = receiveCharUart();
    if (pos < sizeof(longtitude) - 1) longtitude[pos++] = char1;
  } while (char1 != ',');
  longtitude[pos - 1] = '\0';
  pos = 0;

  // kopiuj SZEROKOŚĆ geograficzną do przecinka
  do {
    char1 = receiveCharUart();
    if (pos < sizeof(latitude) - 1) latitude[pos++] = char1;
  } while (char1 != ',');
  latitude[pos - 1] = '\0';
  pos = 0;

  // kopiuj DATĘ i CZAS UTC do końca linii (do bufora response)
  do {
    char1 = receiveCharUart();
    if (pos < BUFFER_SIZE - 1) response[pos++] = char1;
  } while (char1 != '\r' && char1 != '\n');
  response[pos - 1] = '\0';
  receiveCharUart();   // odczytaj pozostały znak CR/LF

  return 1;
}

// *********************************************************************************************
// Odczyt numeru dzwoniącego z URC: +CLIP: "+48123456789",145,...
// *********************************************************************************************
uint8_t readPhoneNumber(void) {
  uint8_t char1;
  uint8_t pos = 0;

  // czekaj na dwukropek, potem na pierwszy cudzysłów
  do { char1 = receiveCharUart(); } while (char1 != ':');
  do { char1 = receiveCharUart(); } while (char1 != '\"');

  // kopiuj numer do zamykającego cudzysłowu
  do {
    char1 = receiveCharUart();
    if (pos < sizeof(phonenumber) - 1) phonenumber[pos++] = char1;
  } while (char1 != '\"');
  phonenumber[pos - 1] = '\0';

  // doczytaj do końca linii, żeby opróżnić bufor odbiorczy
  do { char1 = receiveCharUart(); } while (char1 != 0x0a && char1 != 0x0d);
  return 1;
}

// *********************************************************************************************
// Inicjalizacja SIM800L
// *********************************************************************************************

// Czekaj aż moduł odpowie OK na AT (SIM800L po starcie robi autodetekcję prędkości)
uint8_t checkAt(void) {
  uint8_t initialized = 0;
  do {
    sendStringPgm(AT);
    if (readLine() > 0) {
      if (responseContains_P(ISOK, sizeof(ISOK))) initialized = 1;
    }
  } while (initialized == 0);
  delaySec(1);
  sendStringPgm(ECHO_OFF);   // wyłącz echo komend - upraszcza parsowanie odpowiedzi
  delaySec(1);
  return 1;
}

// Sprawdź status PIN karty SIM, w razie potrzeby podaj kod PIN
uint8_t checkPin(void) {
  uint8_t initialized = 0;
  do {
    delaySec(2);
    sendStringPgm(SHOW_PIN);
    if (readLine() > 0) {
      if (responseContains_P(PIN_IS_READY, sizeof(PIN_IS_READY))) initialized = 1;
      if (responseContains_P(PIN_MUST_BE_ENTERED, sizeof(PIN_MUST_BE_ENTERED))) {
        sendStringPgm(ENTER_PIN);
      }
    }
  } while (initialized == 0);
  return 1;
}

// Czekaj na rejestrację w sieci 2G; przy braku zasięgu: tryb samolotowy + sen na 30 minut
uint8_t checkRegistration(void) {
  uint8_t initialized = 0, nbrminutes;
  do {
    // daj rozsądny czas na wyszukanie sieci 2G (mogliśmy być w ruchu)
    delaySec(3);
    sendStringPgm(FLIGHT_OFF);   // włącz radio i szukaj sieci
    delaySec(60);
    sendStringPgm(SHOW_REGISTRATION);
    if (readLine() > 0) {
      if (responseContains_P(ISREG1, sizeof(ISREG1))) initialized = 1;
      if (responseContains_P(ISREG2, sizeof(ISREG2))) initialized = 1;
      if (initialized == 0) {
        // brak sieci: wyłącz radio i uśpij moduł na 30 minut (nie drenuj baterii w garażu)
        sendStringPgm(FLIGHT_ON);
        delaySec(1);
        sendStringPgm(SLEEP_ON);
        for (nbrminutes = 0; nbrminutes < 30; nbrminutes++) {
          delaySec(60);
        }
        // obudź SIM800L i szukaj sieci ponownie
        sendStringPgm(AT);
        delaySec(1);
        sendStringPgm(SLEEP_OFF);
        delaySec(1);
      }
    }
  } while (initialized == 0);
  return 1;
}

// Skonfiguruj parametry GPRS/APN (bez sprawdzania błędów - unikamy zakleszczeń)
uint8_t provisionGprs(void) {
  delaySec(4);
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

  uint8_t initialized, attempt;
  uint16_t nbrseconds = 0;

  initUart();

  // PD2 jako wejście z podciąganiem - tu podłączony jest pin RI/RING modułu SIM800L
  DDRD &= ~(1 << PD2);
  PORTD |= (1 << PD2);

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

  // PIN karty SIM i rejestracja w sieci
  checkPin();
  checkRegistration();

  // pętla główna - nigdy się nie kończy
  while (1) {

    do {
      // usuń wszystkie SMSy, żeby nie zapełnić pamięci karty SIM
      sendStringPgm(SMS_TEXTMODE);
      delaySec(2);
      sendStringPgm(DELSMS);

      // skonfiguruj GPRS (potrzebny do zapytania o pozycję BTS)
      provisionGprs();

      // włącz prezentację numeru dzwoniącego (potrzebna do odesłania SMS)
      sendStringPgm(CLIP_ON);
      delaySec(2);

      // zamknij kontekst IP - na wypadek gdyby został otwarty
      sendStringPgm(SAPBRCLOSE);
      delaySec(2);

      initialized = 0;

      // uśpij SIM800L - obudzi go przychodzące połączenie/SMS
      sendStringPgm(SLEEP_ON);
      delaySec(2);

      // Czekaj na stan NISKI na pinie RI/RING (PD2), sprawdzając co 1 sekundę.
      // Odpytujemy pin, bo impuls RI przy SMS trwa tylko 120ms - przy połączeniu
      // głosowym RI jest w stanie niskim przez cały czas dzwonienia.
      // Co 30 minut budzimy moduł i sprawdzamy zasięg 2G.
      while (initialized == 0) {
        if ((PIND & (1 << PD2)) == 0) {
          initialized = 1;   // RI = LOW: przychodzące połączenie - obsłuż je
        } else {
          nbrseconds++;
          delaySec(1);
          if (nbrseconds == 1800) {
            // 30 minut minęło - obudź moduł i sprawdź zasięg
            nbrseconds = 0;
            sendStringPgm(AT);
            delaySec(1);
            sendStringPgm(SLEEP_OFF);
            checkRegistration();
            delaySec(1);
            sendStringPgm(SLEEP_ON);
            delaySec(2);
            initialized = 0;   // dalej czekamy na RING
          }
        }
      }

      // RI opadł - sprawdź co przyszło z modemu (RING? SMS? restart modułu?)
      if (readLine() > 0) {
        if (responseContains_P(ISRING, sizeof(ISRING))) {
          initialized = 1;
          readPhoneNumber();       // zapamiętaj numer dzwoniącego z +CLIP
          // wybudź moduł, odrzuć połączenie i przejdź do wysyłki SMS
          sendStringPgm(AT);
          delaySec(1);
          sendStringPgm(SLEEP_OFF);
          delaySec(1);
          sendStringPgm(HANGUP);
          delaySec(1);
        } else {
          // coś innego niż RING (np. SMS albo restart modułu) - zignoruj,
          // wybudź moduł, sprawdź PIN i wróć na początek pętli oczekiwania
          sendStringPgm(AT);
          delaySec(1);
          sendStringPgm(SLEEP_OFF);
          delaySec(2);
          checkPin();
          initialized = 0;
        }
      }
    } while (initialized == 0);

    // Połącz z GPRS - do 3 prób
    initialized = 0;
    attempt = 0;
    do {
      sendStringPgm(SAPBRCLOSE);   // zamknij kontekst na wypadek błędu
      delaySec(2);
      sendStringPgm(SAPBROPEN);    // otwórz kontekst IP (GPRS attach)
      delaySec(5);
      sendStringPgm(SAPBRQUERY);   // sprawdź czy dostaliśmy adres IP
      if (readLine() > 0) {
        if (responseContains_P(SAPBRSUCC, sizeof(SAPBRSUCC))) initialized = 1;
      }
      attempt++;
    } while ((attempt < 3) && (initialized == 0));

    // GPRS działa - pobierz pozycję najbliższego BTS i wyślij SMS
    if (initialized == 1) {
      delaySec(1);
      sendStringPgm(CHECKGPS);
      readCellGps();   // parsuj współrzędne i czas do buforów

      // wyślij SMS w trybie tekstowym na numer dzwoniącego
      delaySec(1);
      sendStringPgm(SMS_TEXTMODE);
      delaySec(1);
      sendStringPgm(SMS_SEND);       // AT+CMGS="
      sendStringUart(phonenumber);
      sendStringPgm(QUOTE_CRLF);     // "<CRLF> - modem odpowie znakiem zachęty '>'
      delaySec(1);
      // treść SMS: data/czas UTC, współrzędne i link do Google Maps
      sendStringUart(response);      // data i czas z odpowiedzi CIPGSMLOC
      sendStringPgm(LONG);
      sendStringUart(longtitude);
      sendStringPgm(LATT);
      sendStringUart(latitude);
      sendStringPgm(GOOGLELOC1);     // http://maps.google.com/maps?q=
      sendStringUart(latitude);
      sendStringPgm(COMMA);
      sendStringUart(longtitude);
      sendStringPgm(CRLF);
      delaySec(1);
      sendCharUart(26);              // CTRL+Z kończy treść SMS i wysyła go

      // daj czas na wysyłkę i zamknij kontekst IP
      delaySec(10);
      sendStringPgm(SAPBRCLOSE);
    }

    delaySec(10);
    // wróć na początek pętli: konfiguracja, uśpienie SIM800L i czekanie na kolejny RING
  }
}
