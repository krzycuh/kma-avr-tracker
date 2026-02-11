# Zarys projektu: GPS Tracker na ATtiny

## Cel projektu

Zbudowanie autonomicznego trackera GPS opartego na mikrokontrolerze z rodziny ATtiny (AVR),
który odbiera dane pozycyjne z modułu GPS, przetwarza je i przekazuje dalej
do systemu nadrzędnego (Raspberry Pi lub inny odbiornik) przez UART.

Projekt ma charakter edukacyjno-hobbystyczny z potencjałem rozwoju do samodzielnego
urządzenia śledzącego.

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
│   ├── uart-receiver-test.py       # Odbiornik danych UART (Python/RPi)
│   └── uart-request-test.py        # Test request-response UART (Python/RPi)
├── .gitignore
└── README.md
```

### Co działa

1. **Komunikacja UART ATtiny → RPi** — przetestowana, 9600 bps, 8N1, U2X
2. **Komunikacja dwukierunkowa** — przerwania RX, bufor kołowy 16 B, echo
3. **Narzędzia testowe Python** — odbiór i wysyłka danych przez `/dev/ttyS0`
4. **Sygnalizacja LED** — heartbeat (PD2) i aktywność RX/TX (PD3)

### Czego brakuje

- Odbiór i parsowanie danych GPS (NMEA)
- Integracja modułu GPS z ATtiny
- System budowania (CMake usunięty, brak alternatywy)
- Zarządzanie energią
- Jakikolwiek format wyjściowy pozycji
- Testy automatyczne
- Dokumentacja sprzętowa (schematy, BOM)

---

## Docelowa architektura (propozycja)

### Wariant A: ATtiny jako bridge GPS → UART → RPi

```
┌─────────────┐    UART (9600)    ┌──────────────┐    UART     ┌─────────────┐
│  Moduł GPS  │ ──────────────► │   ATtiny2313  │ ──────────► │ Raspberry Pi │
│  (NEO-6M)   │    NMEA sentences │  (parser +    │  parsed     │ (agregacja,  │
│             │                   │   filtr)      │  data       │  wizualizacja│
└─────────────┘                   └──────────────┘             └─────────────┘
                                    │       │
                                   LED     LED
                                  (PD2)   (PD3)
                                  status  activity
```

**Problem:** ATtiny2313 ma 1 UART sprzętowy. Jeśli GPS zajmuje UART RX,
to komunikacja z RPi wymaga software UART (bitbanging) na innym pinie,
albo multipleksowania (np. GPS podłączony tylko do RX, TX idzie do RPi).

**Rozwiązanie półdupleksowe:** GPS → UART RX (ATtiny nasłuchuje NMEA),
ATtiny → UART TX → RPi (ATtiny wysyła przetworzone dane). Jednokierunkowy
przepływ danych, 1 UART wystarcza.

### Wariant B: ATtiny jako samodzielny logger

```
┌─────────────┐    UART     ┌──────────────┐    I2C/SPI    ┌──────────────┐
│  Moduł GPS  │ ──────────► │   ATtiny841   │ ───────────► │  EEPROM /    │
│             │   NMEA      │  (parser +    │              │  Flash       │
└─────────────┘             │   logger)     │              └──────────────┘
                            └──────────────┘
                               │
                              LED
```

Wymaga MCU z większą pamięcią i ewentualnie 2 UART.

### Wariant C: ATtiny + GSM (samodzielny tracker)

```
┌─────────────┐  UART1   ┌──────────────┐  UART2   ┌──────────────┐
│  Moduł GPS  │ ──────► │  ATtiny841 /  │ ──────► │   SIM800L    │ → SMS/HTTP
│             │         │  ATmega328P   │         │              │
└─────────────┘         └──────────────┘         └──────────────┘
```

Najbardziej złożony, wymaga MCU z 2 UART lub software UART, wyższy pobór prądu.

---

## Rekomendacja: MVP (Minimum Viable Product)

### Zakres MVP

**Wariant A w wersji półdupleksowej** — najprostszy, buduje na istniejącym kodzie:

1. Moduł GPS (np. NEO-6M) podłączony do UART RX ATtiny2313
2. ATtiny parsuje zdanie `$GPRMC` w locie (bez buforowania pełnego zdania)
3. ATtiny wysyła przetworzoną pozycję (lat, lon, time, speed) przez UART TX
4. Raspberry Pi odbiera i wyświetla/loguje dane
5. LED sygnalizuje: fix GPS, aktywność transmisji

### Ograniczenia MVP

- Brak dwukierunkowej komunikacji z RPi (GPS zajmuje RX)
- Brak zarządzania energią
- Brak logowania na pamięć nieulotną
- Brak GSM/LoRa
- Zależność od RPi jako odbiornika

### Po MVP — ścieżki rozwoju

1. **Software UART** — odzyskanie dwukierunkowej komunikacji z RPi
2. **Sleep mode** — budzenie co N sekund, odczyt GPS, wysyłka, uśpienie
3. **Migracja na ATtiny841/1614** — 2 UART sprzętowe, więcej pamięci
4. **Moduł GSM** — autonomiczna transmisja SMS/HTTP
5. **Logowanie trasy** — zewnętrzny EEPROM/Flash przez I2C/SPI
6. **PCB** — dedykowana płytka z kompaktowym form factor

---

## Stos technologiczny

| Warstwa | Technologia |
|---------|-------------|
| MCU | ATtiny2313 (MVP), ATtiny841/1614 (rozszerzenie) |
| Język firmware | C (avr-gcc, avr-libc) |
| GPS | Moduł UART, protokół NMEA 0183 |
| Komunikacja | UART 9600 bps 8N1 |
| Testy firmware | Unity/CMocka na hoście + Python integration tests |
| Testy UART | Python + pyserial (istniejące skrypty) |
| Symulacja | simavr (opcjonalnie) |
| Build system | CMake + avr-gcc toolchain (do przywrócenia) lub PlatformIO |
| Programator | USBasp / AVRISP mkII / Arduino as ISP |
| Host | Raspberry Pi (odbiornik, narzędzia testowe) |
| VCS | Git (GitHub) |

---

## Schemat pinów ATtiny2313 (MVP)

```
                    ATtiny2313
                  ┌─────────────┐
        RESET ───┤1  PA2   VCC 20├─── VCC (3.3V/5V)
              ───┤2  PD0   PB7 19├───
              ───┤3  PD1   PB6 18├───
              ───┤4  PA1   PB5 17├───
              ───┤5  PA0   PB4 16├───
              ───┤6  PD2   PB3 15├───  (MOSI - ISP)
              ───┤7  PD3   PB2 14├───  (MISO - ISP)
              ───┤8  PD4   PB1 13├───  (SCK  - ISP)
              ───┤9  PD5   PB0 12├───
          GND ───┤10 GND   PD6 11├───
                  └─────────────┘

  Przypisania MVP:
  - PD0 (RXD)  ← GPS TX (odbiór NMEA)
  - PD1 (TXD)  → RPi RX  (wysyłka przetworzonych danych)
  - PD2        → LED status (heartbeat / GPS fix)
  - PD3        → LED activity (transmisja danych)
  - PB5/PB3    → MOSI (ISP programming)
  - PB4/PB2    → MISO (ISP programming)
  - PB3/PB1    → SCK  (ISP programming)
```

---

## Kluczowe ryzyka

| Ryzyko | Wpływ | Mitygacja |
|--------|-------|-----------|
| RAM ATtiny2313 (128 B) za mały na parsowanie NMEA | Wysoki | Parsowanie strumieniowe (character-by-character state machine), bez buforowania pełnych zdań |
| Flash ATtiny2313 (2 KB) za mały na cały firmware | Wysoki | Optymalizacja rozmiaru kodu (`-Os`), minimalistyczny parser, ewentualna migracja MCU |
| Baud rate error z wewnętrznym oscylatorem | Średni | U2X bit (0.2% error), ewentualnie zewnętrzny kwarc 7.3728 MHz |
| Jeden UART sprzętowy dla dwóch urządzeń | Średni | Architektura półdupleksowa (GPS→RX, TX→RPi) lub software UART |
| Zimny start GPS (TTFF) do 12 minut | Niski | Backup battery na module GPS, hot/warm start |
