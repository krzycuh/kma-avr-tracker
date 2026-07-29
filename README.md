# kma-avr-tracker

GPS tracker na **ATtiny2313 + SIM800L**: po zadzwonieniu na numer karty SIM tracker
odrzuca połączenie, pobiera przez GPRS pozycję najbliższej stacji bazowej GSM i odsyła
SMS z linkiem do Google Maps. Bazuje na projekcie
[mcore1976/gpstracker](https://github.com/mcore1976/gpstracker) (`main3b.c`).

- **Plan projektu i kamienie milowe:** [docs/PLAN.md](docs/PLAN.md)
- **Projekt układu (połączenia, zasilanie, BOM):** [docs/SCHEMAT.md](docs/SCHEMAT.md)
- Środowisko (kompilacja `avr-build` + avrdude/linuxgpio na RPi):
  [kma-avr-env](https://github.com/krzycuh/kma-avr-env)

## Programy AVR (`avr-app/`)

### gps-tracker.c — docelowy firmware trackera
- Port `main3b.c` z projektu referencyjnego, dostosowany do stylu repo; 1786 B flasha (87%).
- Konfiguracja SIM800L (9600 bps, PIN, rejestracja 2G), sen modułu, czekanie na RI (PD2),
  po połączeniu: GPRS → `AT+CIPGSMLOC` → SMS z linkiem do mapy na numer dzwoniącego.
- Przed wgraniem uzupełnij sekcję **KONFIGURACJA** (PIN karty, APN operatora).
- Uwaga: PD2 to wejście RI — odłącz diodę LED używaną w programach testowych.

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
- Udaje moduł SIM800L na porcie szeregowym RPi: odpowiada na komendy AT, obsługuje tryb
  SMS (znak zachęty `>` + Ctrl+Z), zwraca sztuczną pozycję z `AT+CIPGSMLOC`.
- Komenda `ring` symuluje przychodzące połączenie (`RING` + `+CLIP`), z `--ri-pin N`
  steruje też linią RI podłączoną do PD2.
- Pozwala przetestować cały `gps-tracker.c` bez fizycznego modułu — scenariusze w
  [docs/PLAN.md](docs/PLAN.md) (M1–M2).

### uart-request-test.py / uart-receiver-test.py — testy UART
- Proste skrypty do testów echa i odbioru danych z AVR.
