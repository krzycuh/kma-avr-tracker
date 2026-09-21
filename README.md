# kma-avr-tracker

GPS tracker na **ATmega328P + SIM7000E** (Waveshare NB-IoT/LTE/GPRS/GPS HAT):
cyklicznie pobiera pozycję z **prawdziwego odbiornika GNSS** modułu (`AT+CGNSINF`)
i wysyła ją **HTTP GET-em na serwer REST** (`?token=...&lat=...&lon=...`) przez
LTE Cat-M1 / NB-IoT / 2G (fallback). Bez dzwonienia i SMS-ów.

Projekt zaczynał na ATtiny2313 + SIM800L (inspiracja:
[mcore1976/gpstracker](https://github.com/mcore1976/gpstracker)) — historia migracji
w gicie; stare programy ATtiny zostały jako referencja.

- **Plan projektu i kamienie milowe:** [docs/PLAN.md](docs/PLAN.md)
- **Projekt układu (połączenia, zasilanie, BOM):** [docs/SCHEMAT.md](docs/SCHEMAT.md)
- Środowisko (kompilacja `avr-build` + avrdude/linuxgpio na RPi):
  [kma-avr-env](https://github.com/krzycuh/kma-avr-env) — wymaga rozszerzenia
  o `-mmcu=atmega328p` / `-p m328p`

## Programy AVR (`avr-app/`)

### gps-tracker.c — docelowy firmware trackera (ATmega328P)
- Co `REPORT_INTERVAL_MIN` minut: fix GNSS (`AT+CGNSINF`, do 3 min czekania) →
  połączenie danych (`AT+CNACT`) → HTTP GET klientem „SH" modułu (`AT+SHREQ`) →
  rozłączenie; dioda PD3 miga 3× po potwierdzonym raporcie (HTTP 200).
- Odporność: timeout ~5 s na znak UART, impuls na pin PWR HAT-a gdy moduł milczy,
  pominięcie cyklu bez fixu, tryb samolotowy przy dłuższym braku sieci.
- Przed wgraniem uzupełnij sekcję **KONFIGURACJA** (PIN karty, APN, `API_SERVER`,
  `API_PATH`, `API_TOKEN`, interwał). 1984 B z 32 KB flasha.

### at-test.c — test komend AT (ATmega328P)
- Krok pośredni: cyklicznie wysyła `AT`, po rozpoznaniu `OK` miga diodą PD3.
- Do testów z symulatorem albo z prawdziwym HAT-em.

### attiny-rpi-2way-com.c / attiny-rpi-com.c — programy historyczne (ATtiny2313)
- Echo UART (przerwania + bufor pierścieniowy) i test nadawania — z pierwszego etapu
  projektu; działający punkt odniesienia dla komunikacji UART.

## Narzędzia na RPi (`uart-test/`)

### sim7000-simulator.py — symulator SIM7000E
- Udaje moduł na porcie szeregowym RPi: CPIN/CGATT/CNACT (+URC `+APP PDP: ACTIVE`),
  GNSS z symulacją łapania fixu (`AT+CGNSINF` — fix po 3 zapytaniach) i klient HTTP
  „SH" (`+SHREQ: "GET",200`).
- Gdy firmware wysyła raport, wypisuje pełny URL z rozbitym tokenem i współrzędnymi —
  test całego `gps-tracker.c` bez fizycznego HAT-a.
- Komendy interaktywne: `nofix`/`fix`, `noreg`/`reg`, `httpfail`, `quit`.

### uart-request-test.py / uart-receiver-test.py — testy UART
- Proste skrypty do testów echa i odbioru danych z AVR.

## Serwer (`server-test/`)

### location-server.py — przykładowy endpoint REST
- Czysty Python (stdlib): `GET /api/location?token=...&lat=...&lon=...` → walidacja
  tokenu (403 przy złym), dopisanie pozycji z timestampem do CSV, link do Google Maps.
- Uruchomienie: `python3 server-test/location-server.py --port 8080 --token twoj-token`.
- Musi być osiągalny z publicznego internetu — opcje w [docs/PLAN.md](docs/PLAN.md) (M3).
