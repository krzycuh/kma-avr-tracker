# kma-avr-tracker

GPS tracker na **ATtiny2313 + SIM800L**: cyklicznie pobiera przez GPRS pozycję najbliższej
stacji bazowej GSM (`AT+CIPGSMLOC`) i wysyła ją **HTTP GET-em na serwer REST**
(`?token=...&lat=...&lon=...`). Bez dzwonienia i SMS-ów. Inspirowany projektem
[mcore1976/gpstracker](https://github.com/mcore1976/gpstracker).

- **Plan projektu i kamienie milowe:** [docs/PLAN.md](docs/PLAN.md)
- **Projekt układu (połączenia, zasilanie, BOM):** [docs/SCHEMAT.md](docs/SCHEMAT.md)
- Środowisko (kompilacja `avr-build` + avrdude/linuxgpio na RPi):
  [kma-avr-env](https://github.com/krzycuh/kma-avr-env)

## Programy AVR (`avr-app/`)

### gps-tracker.c — docelowy firmware trackera
- Co `REPORT_INTERVAL_MIN` minut: GPRS attach → `AT+CIPGSMLOC` → HTTP GET na serwer
  (wbudowany klient HTTP SIM800L) → sen modułu (`AT+CSCLK=2`); 1762 B flasha (86%).
- Odporność: timeout ~5 s na znak UART, 3 próby GPRS, rekonfiguracja po restarcie modułu,
  tryb samolotowy przy braku zasięgu 2G. Dioda PD3 miga 3× po potwierdzonym raporcie (HTTP 200).
- Przed wgraniem uzupełnij sekcję **KONFIGURACJA** (PIN karty, APN, `API_URL`, `API_TOKEN`,
  interwał). Uwaga: URL musi być `http://` — SIM800L nie obsługuje współczesnego TLS.

### sim800l-test.c — test komend AT
- Krok pośredni: cyklicznie wysyła `AT`, po odebraniu i rozpoznaniu `OK` miga diodą PD3.
- Do testów z symulatorem albo z prawdziwym modułem.

### attiny-rpi-2way-com.c — echo UART
- Dwukierunkowa komunikacja UART 9600 bps (8N1, z U2X) na ATtiny.
- Odbiór w przerwaniu z buforem pierścieniowym 16 B; główna pętla odczytuje bufor i odsyła te same dane (echo).
- LED PD2: sygnalizacja pracy („heartbeat"). LED PD3: aktywność RX/TX; na starcie obie migają 3×.
- Proste funkcje wysyłania znaków/łańcuchów przez UART ułatwiają integrację i testy z RPi.

### attiny-rpi-com.c — test nadawania
- UART 9600 bps (8N1, z U2X); transmisja bez przerwań (polling).
- Okresowo wysyła przykładowy tekst przez UART; dioda na PD2 sygnalizuje wysyłanie.
- Prosty szkic do testów linii TX i integracji z RPi/terminalem szeregowym.

## Narzędzia na RPi (`uart-test/`)

### sim800l-simulator.py — symulator SIM800L
- Udaje moduł SIM800L na porcie szeregowym RPi: odpowiada na komendy AT (CPIN, CREG,
  SAPBR, CIPGSMLOC) i emuluje klienta HTTP (`+HTTPACTION: 0,200`).
- Gdy firmware ustawia URL raportu, wypisuje go wraz z rozbitym tokenem i współrzędnymi —
  pozwala przetestować cały `gps-tracker.c` bez fizycznego modułu.
- Komendy interaktywne: `noreg`/`reg` (symulacja braku zasięgu), `httpfail` (błąd HTTP 601).
- Scenariusze testów w [docs/PLAN.md](docs/PLAN.md) (M1–M2).

### uart-request-test.py / uart-receiver-test.py — testy UART
- Proste skrypty do testów echa i odbioru danych z AVR.

## Serwer (`server-test/`)

### location-server.py — przykładowy endpoint REST
- Czysty Python (stdlib): `GET /api/location?token=...&lat=...&lon=...` → walidacja tokenu
  (403 przy złym), dopisanie pozycji z timestampem do CSV, link do Google Maps w logu.
- Uruchomienie: `python3 server-test/location-server.py --port 8080 --token twoj-token`.
- Musi być osiągalny z publicznego internetu (tracker łączy się z sieci operatora) —
  opcje hostingu w [docs/PLAN.md](docs/PLAN.md), kamień M3.
