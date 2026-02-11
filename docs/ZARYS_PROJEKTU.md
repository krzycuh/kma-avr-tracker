# Zarys projektu: GPS Tracker na AVR

## Cel projektu

Zbudowanie autonomicznego, taniego trackera GPS opartego na mikrokontrolerze AVR,
który:

1. Odbiera pozycję z modułu GPS
2. Wysyła ją przez sieć komórkową (moduł SIM) na endpoint HTTP
3. Działa na baterii z jak najniższym poborem prądu
4. Składa się z minimalnej liczby komponentów

Projekt ma charakter edukacyjno-hobbystyczny. Raspberry Pi służy **wyłącznie
do testowania** (UART debug, replay NMEA, weryfikacja firmware) — nie jest
częścią docelowego urządzenia.

---

## Stan obecny repozytorium

### Co już istnieje

```
kma-avr-tracker/
├── avr-app/
│   ├── attiny-rpi-com.c            # Prosty nadajnik UART (polling)
│   └── attiny-rpi-2way-com.c       # Dwukierunkowy UART z przerwaniami i buforem kołowym
├── uart-test/
│   ├── requirements.txt            # pyserial>=3.5
│   ├── uart-receiver-test.py       # Odbiornik danych UART (Python, do testów)
│   └── uart-request-test.py        # Test request-response UART (Python, do testów)
├── .gitignore
└── README.md
```

### Co działa

1. **Komunikacja UART** — przetestowana na ATtiny2313, 9600 bps, 8N1, U2X
2. **Komunikacja dwukierunkowa** — przerwania RX, bufor kołowy 16 B, echo
3. **Narzędzia testowe Python** — odbiór i wysyłka danych przez `/dev/ttyS0`
4. **Sygnalizacja LED** — heartbeat (PD2) i aktywność RX/TX (PD3)

### Posiadany sprzęt

- ATtiny2313 (w repo, ale za mały na docelowy firmware)
- SIM800L (moduł GSM/GPRS 2G — posiadany)
- Raspberry Pi (do testowania)

### Czego brakuje

- **Moduł GPS** — do zakupu
- **MCU z większymi zasobami** — ATtiny2313 nie udźwignie AT komend SIM + parsowania NMEA
- Firmware: parsowanie NMEA, obsługa AT komend SIM, HTTP POST
- Zarządzanie energią (sleep mode, sterowanie zasilaniem modułów)
- System budowania (CMake usunięty)
- Dokumentacja sprzętowa (schematy, BOM)

---

## Dlaczego ATtiny2313 nie wystarczy

| Zasób | ATtiny2313 | Potrzeby firmware | Werdykt |
|-------|-----------|-------------------|---------|
| Flash | 2 KB | Parser NMEA ~1 KB + AT komendy SIM ~3-5 KB + main loop ~1 KB = **~5-7 KB** | **Za mało** |
| RAM | 128 B | Bufor UART 32 B + stan parsera 30 B + bufor AT response 64 B + zmienne = **~160+ B** | **Za mało** |
| UART | 1 sprzętowy | GPS (UART1) + SIM800L (UART2) = **2 kanały** | **Za mało** |

### Rekomendowane MCU (pozostajemy w ekosystemie AVR)

| MCU | Flash | RAM | UART | Cena | Ocena |
|-----|-------|-----|------|------|-------|
| **ATmega328P** | 32 KB | 2 KB | 1 HW + SW UART | ~8 PLN | Najlepszy stosunek cena/możliwości, ogromna baza wiedzy (Arduino) |
| ATtiny841 | 8 KB | 512 B | 2 HW | ~6 PLN | 2 UART ale ciasno z pamięcią na AT komendy |
| ATtiny1614 | 16 KB | 2 KB | 1 HW | ~5 PLN | Nowoczesny, ale wymaga UPDI (inny programator) |
| ATmega168P | 16 KB | 1 KB | 1 HW | ~6 PLN | Kompromis — wystarczający jeśli firmware zmieści się w 16 KB |

**Rekomendacja: ATmega328P** — tanio, dużo pamięci, łatwo dostępny, pin-compatible
z Arduino (łatwe prototypowanie), ogromna dokumentacja, software UART dobrze
udokumentowany.

---

## Docelowa architektura

### Schemat systemu

```
┌─────────────┐  UART (9600)   ┌───────────────┐  UART (9600/115200)  ┌──────────────┐
│  Moduł GPS  │ ─────────────► │               │ ────────────────────► │   SIM800L    │
│  (NMEA out) │   HW UART RX   │  ATmega328P   │   SW UART TX/RX      │   (GPRS)     │
└─────────────┘                │               │                      └──────┬───────┘
                               │  Main loop:    │                             │
                               │  1. Parsuj NMEA│                        Sieć 2G/GPRS
                               │  2. Wyślij AT  │                             │
                               │  3. Sleep      │                             ▼
                               └───────┬───────┘                      ┌──────────────┐
                                  │    │    │                          │  HTTP Server │
                                 LED  LED  MOSFET                     │  (endpoint)  │
                                 fix  act  GPS pwr                    └──────────────┘
```

### Przepływ danych

```
GPS fix ──NMEA──► ATmega328P ──AT+HTTPACTION──► SIM800L ──HTTP POST──► serwer
                       │
                       ├── LED: status GPS fix
                       ├── LED: aktywność transmisji
                       └── sleep między cyklami
```

### Cykl pracy (duty cycle)

```
┌──────┐   ┌──────────────────────┐   ┌────────────────────────┐   ┌──────┐
│SLEEP │──►│ WAKE: włącz GPS,     │──►│ Wyślij HTTP POST       │──►│SLEEP │
│(deep)│   │ czekaj na fix        │   │ przez SIM800L          │   │      │
│~0.1µA│   │ parsuj NMEA          │   │ AT komendy             │   │      │
│ N min│   │ ~30-60s, ~45mA (GPS) │   │ ~5-15s, ~200mA (GSM)  │   │      │
└──────┘   └──────────────────────┘   └────────────────────────┘   └──────┘
```

---

## MVP (Minimum Viable Product)

### Zakres MVP

1. ATmega328P odbiera NMEA z GPS, parsuje `$GPRMC` (pozycja, czas, prędkość)
2. ATmega328P wysyła dane przez SIM800L na endpoint HTTP (POST/GET)
3. Cykl: odczyt GPS → wysłanie HTTP → powtórz co N minut
4. LED sygnalizacja: GPS fix, transmisja GSM
5. Zasilanie z baterii LiPo (3.7V) — idealne dla SIM800L (3.4–4.4V)

### Czego MVP NIE obejmuje

- Sleep mode / zarządzanie energią (dodane w Fazie 4)
- Bufor pozycji offline (jeśli brak zasięgu)
- Geofencing / alarmy
- Dashboard / aplikacja mobilna
- Własny PCB

### Po MVP — ścieżki rozwoju

1. **Sleep mode** — Watchdog Timer, MOSFET na zasilaniu GPS
2. **Bufor offline** — zapis do EEPROM gdy brak zasięgu, wysyłka po powrocie
3. **Migracja SIM800L → SIM7000** — jeśli 2G zostanie wyłączone
4. **PCB** — miniaturyzacja, eliminacja breadboarda
5. **Pomiar baterii** — ADC, raportowanie poziomu naładowania
6. **OTA config** — zmiana interwału raportowania przez odpowiedź HTTP

---

## Stos technologiczny

| Warstwa | Technologia |
|---------|-------------|
| MCU | ATmega328P (docelowo), ATtiny2313 (istniejące eksperymenty UART) |
| Język firmware | C (avr-gcc, avr-libc) |
| GPS | Moduł UART z NMEA 0183 (do wyboru — patrz `WYMAGANIA_GPS.md`) |
| Cellular | SIM800L (posiadany, 2G GPRS) |
| Protokół do serwera | HTTP GET/POST na zdefiniowany endpoint |
| Build system | CMake + avr-gcc toolchain |
| Programator | USBasp / Arduino as ISP |
| Testowanie | RPi + Python/pyserial (narzędzie testowe, NIE część docelowa) |
| VCS | Git (GitHub) |

---

## Uwaga o SIM800L i sieci 2G

SIM800L działa wyłącznie w sieci 2G (GSM/GPRS). Stan 2G w Polsce (2025/2026):

| Operator | Status 2G |
|----------|-----------|
| Orange | Utrzymuje do ~2028 |
| T-Mobile | Utrzymuje |
| Plus | Utrzymuje |
| Play | **Częściowe wyłączanie** |

**Ryzyko:** Projekt z SIM800L może przestać działać za 2-3 lata.
**Mitygacja:** Architektura modularna — moduł SIM komunikuje się przez AT komendy.
Wymiana SIM800L na SIM7000 (LTE Cat-M1/NB-IoT) wymaga minimalnych zmian w firmware
(te same AT komendy z drobnymi różnicami). Patrz `WYMAGANIA_SPRZET.md` dla
porównania modułów.

---

## Kluczowe ryzyka

| Ryzyko | Wpływ | Mitygacja |
|--------|-------|-----------|
| 2G shutdown (SIM800L) | Wysoki | Modularna architektura AT, łatwa migracja na SIM7000 |
| Peak current SIM800L (2A) | Średni | Kondensator 1000µF+ na zasilaniu SIM, LiPo z wysokim C-rate |
| Cold start GPS 30-60s | Średni | Backup battery na GPS, warm start, buforowanie almanac |
| 1 UART na ATmega328P | Niski | Software UART dobrze udokumentowany, wiele bibliotek |
| Zużycie baterii | Średni | Sleep mode, duty cycle, MOSFET na GPS |
| Zasięg GSM w terenie | Niski | Bufor offline w EEPROM, retry logic |
