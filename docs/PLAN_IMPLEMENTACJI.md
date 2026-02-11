# Plan implementacji: GPS Tracker na ATtiny

## Spis treści

1. [Fazy projektu — przegląd](#fazy-projektu)
2. [Faza 0 — Infrastruktura i toolchain](#faza-0)
3. [Faza 1 — Parser NMEA](#faza-1)
4. [Faza 2 — Integracja GPS z ATtiny](#faza-2)
5. [Faza 3 — Protokół wyjściowy i integracja z RPi](#faza-3)
6. [Faza 4 — Zarządzanie energią](#faza-4)
7. [Faza 5 — Rozszerzenia](#faza-5)
8. [Architektura sprzętu i połączenia](#architektura-sprzetu)
9. [Architektura testowania](#architektura-testowania)
10. [Skille Claude do wgrywania i testowania](#skille-claude)

---

<a id="fazy-projektu"></a>
## Fazy projektu — przegląd

```
Faza 0: Infrastruktura     ──► Faza 1: Parser NMEA    ──► Faza 2: Integracja GPS
(build system, testy,          (logika parsowania,         (hardware, UART config,
 struktura projektu)            testy na hoście)            odczyt z modułu GPS)
                                                                    │
                                                                    ▼
                               Faza 4: Energooszczędność ◄── Faza 3: Protokół wyjściowy
                               (sleep modes, duty cycle)     (format danych, wysyłka
                                                              do RPi, testy e2e)
                                                                    │
                                                                    ▼
                                                            Faza 5: Rozszerzenia
                                                            (GSM, logger, PCB)
```

---

<a id="faza-0"></a>
## Faza 0 — Infrastruktura i toolchain

### Cel
Przywrócić i rozbudować system budowania, ustalić strukturę katalogów,
skonfigurować środowisko do kompilacji, flashowania i testowania.

### Zadania

#### 0.1 Struktura katalogów

```
kma-avr-tracker/
├── docs/                        # Dokumentacja projektu
│   ├── NIEDOPRECYZOWANIA.md
│   ├── ZARYS_PROJEKTU.md
│   └── PLAN_IMPLEMENTACJI.md
├── firmware/
│   ├── src/                     # Kod źródłowy firmware
│   │   ├── main.c               # Punkt wejścia, main loop
│   │   ├── uart.c / uart.h      # Warstwa UART (refaktor z istniejącego kodu)
│   │   ├── nmea_parser.c / .h   # Parser NMEA
│   │   ├── gps.c / gps.h        # Logika GPS (fix, pozycja, czas)
│   │   ├── led.c / led.h        # Sterowanie LED
│   │   └── config.h             # Piny, MCU, baud rate, stałe
│   ├── CMakeLists.txt           # Build system firmware
│   └── cmake/
│       └── avr-gcc-toolchain.cmake
├── test/
│   ├── host/                    # Testy na hoście (x86) — parser NMEA, logika
│   │   ├── test_nmea_parser.c
│   │   ├── test_gps_logic.c
│   │   └── CMakeLists.txt
│   ├── integration/             # Testy integracyjne (Python + hardware)
│   │   ├── test_uart_echo.py
│   │   ├── test_gps_output.py
│   │   ├── nmea_replay.py       # Replay nagranych zdań NMEA przez UART
│   │   └── conftest.py
│   └── fixtures/
│       └── sample_nmea.txt      # Przykładowe zdania NMEA do testów
├── tools/
│   ├── flash.sh                 # Skrypt flashowania ATtiny
│   ├── fuse_config.sh           # Konfiguracja fuse bitów
│   └── serial_monitor.py        # Monitor portu szeregowego
├── avr-app/                     # (legacy — istniejący kod, do refaktoru)
├── uart-test/                   # (legacy — istniejące testy, do migracji)
├── CMakeLists.txt               # Root CMake
├── CMakePresets.json
├── .gitignore
└── README.md
```

#### 0.2 Przywrócenie CMake + toolchain AVR

- Przywrócenie `cmake/avr-gcc-toolchain.cmake` (bazując na historii git)
- Root `CMakeLists.txt` z subtargetami: firmware, testy hostowe
- Presety: `avr-debug`, `avr-release`, `host-test`
- Konfigurowalny MCU i F_CPU przez zmienne CMake

#### 0.3 Skrypt flashowania

- `tools/flash.sh` — wrapper na `avrdude`
- Parametry: plik HEX, programator (USBasp/AVRISP/Arduino), port
- Konfiguracja fuse bitów (clock source, BOD, etc.)

#### 0.4 CI (opcjonalnie)

- GitHub Actions: budowanie firmware (avr-gcc), uruchomienie testów hostowych
- Artefakty: plik `.hex` do pobrania

### Kryteria ukończenia
- [ ] `cmake --build` kompiluje firmware do `.hex`
- [ ] `cmake --build --target test` uruchamia testy hostowe
- [ ] `tools/flash.sh` wgrywa firmware na ATtiny

---

<a id="faza-1"></a>
## Faza 1 — Parser NMEA

### Cel
Zaimplementować lekki, strumieniowy parser zdań NMEA (szczególnie `$GPRMC`)
zoptymalizowany pod ograniczenia ATtiny2313 (128 B RAM, 2 KB Flash).

### Zadania

#### 1.1 Projekt parsera NMEA

**Architektura: maszyna stanów (state machine)**

```
IDLE ──'$'──► HEADER ──','──► FIELDS ──'*'──► CHECKSUM ──'\n'──► DONE
  ▲                                                                │
  └────────────────────── reset ◄──────────────────────────────────┘
```

- **Zero-copy**: parsowanie character-by-character, bez buforowania pełnego zdania
- **Selektywne**: ignorowanie niechcianych zdań (tylko `$GPRMC` lub `$GPGGA`)
- **Minimalny RAM**: ~30–40 B na stan parsera + dane wyjściowe

**Struktura danych wyjściowych:**

```c
typedef struct {
    uint8_t  hours, minutes, seconds;  // Czas UTC
    int32_t  latitude;                 // Szerokość × 10^5 (fixed-point)
    int32_t  longitude;                // Długość × 10^5 (fixed-point)
    uint16_t speed_knots_x10;          // Prędkość × 10
    uint16_t course_x10;              // Kurs × 10
    uint8_t  day, month, year;         // Data
    uint8_t  fix_valid;                // 'A' = valid, 'V' = invalid
} gps_data_t;                          // ~20 B
```

#### 1.2 Implementacja parsera

- `nmea_parser.c` / `nmea_parser.h`
- Funkcja `nmea_feed_char(char c)` — podaje jeden znak do parsera
- Callback lub flaga `nmea_data_ready` gdy pełne zdanie sparsowane
- Weryfikacja checksum XOR
- Fixed-point arytmetyka (bez float — oszczędność Flash i RAM)

#### 1.3 Testy hostowe parsera

- Kompilacja `nmea_parser.c` na x86 (gcc, nie avr-gcc)
- Framework: Unity lub prosty assert-based
- Przypadki testowe:
  - Poprawne zdanie `$GPRMC` → prawidłowe dane
  - Nieprawidłowy checksum → odrzucenie
  - Niekompletne zdanie → brak wyniku
  - Zdania inne niż GPRMC → ignorowane
  - Znaki śmieciowe między zdaniami → odporność
  - Brak fixu GPS (`V`) → flaga invalid
  - Współrzędne ujemne (S, W) → prawidłowy znak

#### 1.4 Optymalizacja rozmiaru

- Analiza `avr-size` — cel: <1 KB Flash na parser
- Eliminacja `printf`/`sprintf` (zbyt duże na AVR)
- Konwersja liczb: własna implementacja `itoa` / fixed-point
- `-Os -flto` flagi kompilacji

### Kryteria ukończenia
- [ ] Parser przechodzi wszystkie testy hostowe
- [ ] Rozmiar kodu parsera <1 KB Flash (zmierzony `avr-size`)
- [ ] Zużycie RAM <50 B (zmierzone `avr-size`)

---

<a id="faza-2"></a>
## Faza 2 — Integracja GPS z ATtiny

### Cel
Podłączyć moduł GPS do ATtiny, odebrać dane NMEA przez UART
i przetworzyć je parserem z Fazy 1.

### Zadania

#### 2.1 Konfiguracja UART dla GPS

- Refaktor `uart.c` z istniejącego kodu (`attiny-rpi-2way-com.c`)
- UART RX z przerwaniami → podawanie znaków do `nmea_feed_char()`
- Bufor kołowy 16–32 B (jeśli RAM pozwala)
- Baud rate 9600 (standard GPS NMEA)

#### 2.2 Podłączenie hardware

```
GPS Module (NEO-6M)          ATtiny2313
┌───────────────┐            ┌─────────────┐
│           TX  ├────────────┤ PD0 (RXD)   │
│           RX  ├──(opcja)───┤ PD1 (TXD)   │  ← komendy do GPS (opcjonalnie)
│          VCC  ├────────────┤ VCC          │
│          GND  ├────────────┤ GND          │
└───────────────┘            └─────────────┘
```

**Uwagi:**
- Poziomy napięć: NEO-6M działa na 3.3V, ATtiny2313 na 3.3V lub 5V
- Jeśli ATtiny na 5V → potrzebny dzielnik napięcia lub konwerter poziomów na linii TX→RX
- Jeśli ATtiny na 3.3V → bezpośrednie połączenie OK

#### 2.3 Konfiguracja modułu GPS (opcjonalnie)

- Wysłanie komend PMTK przez TX do GPS:
  - `$PMTK314,...` — wyłączenie zbędnych zdań (tylko GPRMC)
  - `$PMTK220,1000` — ustawienie update rate na 1 Hz
- Zmniejsza obciążenie parsera

#### 2.4 LED sygnalizacja GPS fix

- PD2: blink wolny = szukanie satelitów, blink szybki / ciągły = fix uzyskany
- PD3: mignięcie przy każdym prawidłowym zdaniu NMEA

#### 2.5 Test na hardware

- Flashowanie firmware na ATtiny
- Podłączenie GPS na zewnątrz (z anteną)
- Weryfikacja: LED sygnalizuje fix, dane parsowane poprawnie
- Podgląd UART TX na oscyloskopie lub analizatorze logicznym

### Kryteria ukończenia
- [ ] ATtiny odbiera zdania NMEA z modułu GPS
- [ ] Parser prawidłowo wyodrębnia pozycję, czas, prędkość
- [ ] LED sygnalizuje stan GPS fix
- [ ] Firmware mieści się w 2 KB Flash

---

<a id="faza-3"></a>
## Faza 3 — Protokół wyjściowy i integracja z RPi

### Cel
Zdefiniować format danych wyjściowych i przesyłać przetworzone dane
z ATtiny do Raspberry Pi przez UART TX.

### Zadania

#### 3.1 Definicja protokołu wyjściowego

**Opcja A: Tekstowy (czytelny)**
```
$TRK,51.10728,17.03864,123456,52.3,180.5,A*XX\r\n
       lat      lon     time   spd  course fix checksum
```

**Opcja B: Binarny (kompaktowy)**
```
[0xAA][len][lat:4B][lon:4B][time:3B][speed:2B][course:2B][fix:1B][crc:1B]
```

Opcja A rekomendowana dla MVP — łatwiejsze debugowanie.

#### 3.2 Implementacja wysyłki

- Formatowanie danych GPS do stringa (bez sprintf — własna konwersja)
- Wysyłka przez UART TX co 1 sekundę (lub przy każdym nowym fixie)
- Prosta konwersja int32 → ASCII z kropką dziesiętną (fixed-point)

#### 3.3 Odbiornik na RPi

- Rozbudowa `uart-receiver-test.py` → `tools/serial_monitor.py`
- Parsowanie protokołu `$TRK,...`
- Wyświetlanie: czas, pozycja, prędkość, status fix
- Opcjonalnie: logowanie do pliku CSV, wyświetlanie na mapie

#### 3.4 Test end-to-end

- ATtiny + GPS (prawdziwy fix lub symulowany NMEA) → UART → RPi
- Skrypt Python weryfikuje poprawność danych
- Porównanie z surowym NMEA (ground truth)

### Połączenie UART: dwa warianty

**Wariant 1: Jednokierunkowy (MVP)**
```
GPS ──TX──► ATtiny RX (PD0)
             ATtiny TX (PD1) ──► RPi RX (/dev/ttyS0)
```
Jeden UART sprzętowy, half-duplex. GPS → ATtiny → RPi. Brak kanału zwrotnego.

**Wariant 2: Z software UART (po MVP)**
```
GPS ──TX──► ATtiny RX (PD0)       Hardware UART
             ATtiny TX (PD1) ──► RPi RX

RPi TX ──► ATtiny soft-RX (PB0)   Software UART (bitbanging)
```
Umożliwia komendy z RPi do ATtiny (zmiana duty cycle, request-response).

### Kryteria ukończenia
- [ ] ATtiny wysyła `$TRK,...` co 1 s przez UART TX
- [ ] RPi odbiera i parsuje dane pozycyjne
- [ ] Test e2e z prawdziwym GPS: pozycja zgadza się z rzeczywistą lokalizacją
- [ ] Test e2e z replay NMEA: dane deterministyczne, zgodne z oczekiwaniami

---

<a id="faza-4"></a>
## Faza 4 — Zarządzanie energią

### Cel
Zredukować pobór prądu dla pracy bateryjnej.

### Zadania

#### 4.1 Sleep mode ATtiny

- Power-down mode między odczytami GPS
- Budzenie przez Watchdog Timer co N sekund
- Sekwencja: sleep → wake → enable UART → czekaj na fix → wyślij → sleep

#### 4.2 Sterowanie zasilaniem GPS

- Tranzystor MOSFET na linii VCC modułu GPS
- Pin ATtiny steruje gate → włączanie/wyłączanie GPS
- Uwaga: cold start GPS po power-off trwa 30–60 s

#### 4.3 Pomiar napięcia baterii

- ADC (jeśli dostępny na MCU) → pomiar przez dzielnik napięcia
- ATtiny2313 nie ma ADC → brak możliwości. Rozważenie migracji MCU.

### Kryteria ukończenia
- [ ] Pobór prądu w sleep <10 µA (ATtiny)
- [ ] Duty cycle: 10 s pracy / 50 s sleep → ~17% duty
- [ ] Czas pracy na baterii 18650 (3000 mAh) > 24 h

---

<a id="faza-5"></a>
## Faza 5 — Rozszerzenia (po MVP)

| Rozszerzenie | Opis | Wymaga |
|-------------|-------|--------|
| Software UART | Dwukierunkowa komunikacja z RPi | Timer, precyzyjne bitbanging |
| Moduł GSM (SIM800L) | Autonomiczna transmisja SMS/HTTP | 2. UART, zasilanie 2A peak |
| LoRa (RFM95W) | Daleki zasięg, niski transfer | SPI, gateway LoRa |
| EEPROM logger | Zapis trasy offline | I2C (24LC256) |
| PCB | Dedykowana płytka | KiCad, producent PCB |
| Migracja MCU | ATtiny841/1614/ATmega328P | Nowy target w CMake |
| Geofencing | Alarm przy wyjściu ze strefy | Obliczenia odległości (Haversine) |
| Dashboard | Mapa w przeglądarce (Leaflet.js) | Serwer na RPi, WebSocket |

---

<a id="architektura-sprzetu"></a>
## Architektura sprzętu i połączenia

### BOM (Bill of Materials) — MVP

| Komponent | Model | Ilość | Uwagi |
|-----------|-------|-------|-------|
| Mikrokontroler | ATtiny2313-20PU | 1 | DIP-20, 2KB Flash, 128B RAM |
| Moduł GPS | NEO-6M (lub BN-220) | 1 | Z anteną ceramiczną |
| LED zielona | 3mm | 1 | Status (PD2) |
| LED żółta | 3mm | 1 | Aktywność (PD3) |
| Rezystor | 330Ω | 2 | Ogranicznik prądu LED |
| Kondensator | 100nF ceramiczny | 1 | Bypass VCC |
| Złącze ISP | 2x3 pin header | 1 | Programowanie |
| Płytka prototypowa | — | 1 | Breadboard lub perfboard |
| Zasilanie | 3.3V regulator lub 3xAA | 1 | 3.3V wspólne dla GPS i ATtiny |

### Schemat połączeń MVP (tekstowy)

```
                    +3.3V
                      │
                 ┌────┴────┐
                 │  100nF   │
                 └────┬────┘
                      │
    ┌─────────────────┼──── VCC (pin 20)
    │                 │
    │   ┌─────────────┼──── GND (pin 10)
    │   │             │
    │   │   GPS NEO-6M│
    │   │   ┌────────┐│
    │   │   │ VCC ───┘│
    │   │   │ GND ────┘
    │   │   │ TX ─────────── PD0/RXD (pin 2)  ← NMEA input
    │   │   │ RX ─────────── PD1/TXD (pin 3)  ← (opcjonalnie komendy PMTK)
    │   │   └────────┘
    │   │
    │   │   ATtiny2313
    │   │   ┌─────────────────────────┐
    │   │   │ PD0 (RXD) ← GPS TX     │
    │   │   │ PD1 (TXD) ──────────────────────── RPi RX (/dev/ttyS0)
    │   │   │ PD2 ──── [330Ω] ──── LED (zielona) ──── GND    STATUS
    │   │   │ PD3 ──── [330Ω] ──── LED (żółta)   ──── GND    ACTIVITY
    │   │   │                                     │
    │   │   │ PB5 (MOSI) ◄──── ISP programmer     │
    │   │   │ PB4 (MISO) ────► ISP programmer     │
    │   │   │ PB3 (SCK)  ◄──── ISP programmer     │
    │   │   │ PA2 (RESET)◄──── ISP programmer     │
    │   │   └─────────────────────────┘
    │   │
    │   └──── GND common (ATtiny, GPS, RPi)
    └──────── VCC common (ATtiny, GPS) — 3.3V
```

### Połączenie ISP (programowanie)

```
ISP Header (2x3):       ATtiny2313:
  MOSI ──────────────── PB5 (pin 17)
  MISO ──────────────── PB4 (pin 16) — UWAGA: w ATtiny2313 ISP używa PB5=MOSI, PB4=MISO, PB7=SCK
  SCK  ──────────────── PB7 (pin 19)
  RESET ─────────────── PA2 (pin 1)
  VCC  ──────────────── VCC (pin 20)
  GND  ──────────────── GND (pin 10)
```

> **UWAGA:** Piny ISP w ATtiny2313 to PB5 (MOSI), PB4 (MISO), PB7 (SCK).
> Różnią się od ATmega328P! Sprawdzić datasheet przed podłączeniem.

### Połączenie ATtiny ↔ RPi (UART)

```
ATtiny2313 TXD (PD1, pin 3) ──── RPi GPIO15 (RXD, pin 10 na header)
ATtiny2313 GND  (pin 10)    ──── RPi GND (pin 6 na header)
```

> **Poziomy napięć:** Jeśli oba na 3.3V — bezpośrednio.
> Jeśli ATtiny na 5V, RPi na 3.3V → dzielnik napięcia na linii TX→RX RPi.

---

<a id="architektura-testowania"></a>
## Architektura testowania

### Piramida testów

```
           ┌──────────────────┐
           │   Testy E2E      │  ← Prawdziwy GPS + ATtiny + RPi
           │  (na hardware)   │     Ręczne lub z replay NMEA
           ├──────────────────┤
           │ Testy integracyjne│  ← Python + UART + ATtiny (flashowany)
           │  (hardware-in-    │     Automatyczne: replay NMEA → weryfikacja
           │   the-loop)       │     wyjścia $TRK
           ├──────────────────┤
           │ Testy hostowe     │  ← Kompilacja na x86 (gcc)
           │ (unit tests)      │     Parser NMEA, logika GPS, formatowanie
           │                   │     Szybkie, deterministyczne, bez hardware
           └──────────────────┘
```

### Testy hostowe (Faza 0–1)

**Cel:** Testowanie logiki parsera NMEA i formatowania danych bez ATtiny.

**Narzędzia:**
- Kompilator: `gcc` (host, nie avr-gcc)
- Framework: Unity (minimalistyczny, single-header) lub prosty assert
- Budowanie: CMake target `host-test`

**Abstrakcja HAL:**
```c
// hal.h — Hardware Abstraction Layer
#ifdef AVR_TARGET
  #include <avr/io.h>
  #define uart_send_char(c) { while(!(UCSRA & (1<<UDRE))); UDR = c; }
#else
  // Host mock — zapis do bufora testowego
  void uart_send_char(uint8_t c);
#endif
```

Pozwala kompilować ten sam `nmea_parser.c` na hoście i na AVR.

### Testy integracyjne (Faza 2–3)

**Cel:** Weryfikacja firmware na prawdziwym ATtiny z symulowanym wejściem GPS.

**Narzędzia:**
- Python 3 + pyserial
- Framework: pytest
- Sprzęt: RPi ↔ ATtiny (UART)

**Replay NMEA:**
```python
# nmea_replay.py — symuluje moduł GPS
# Wysyła nagrane zdania NMEA przez UART do ATtiny RX
import serial, time

def replay_nmea(port, file, baud=9600):
    ser = serial.Serial(port, baud)
    with open(file) as f:
        for line in f:
            ser.write(line.encode())
            time.sleep(0.1)  # symulacja 10 Hz GPS
```

**Fixture NMEA (`test/fixtures/sample_nmea.txt`):**
```
$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
$GPRMC,123520,A,4807.039,N,01131.001,E,022.5,084.5,230394,003.1,W*6E
$GPRMC,123521,V,,,,,,,230394,003.1,W*6D
```

**Schemat testowy:**
```
RPi TX (/dev/ttyS0) ──NMEA──► ATtiny RX (PD0)   ← symulowany GPS
                               ATtiny TX (PD1) ──► RPi RX (drugi port lub USB-UART)
                                                    ↓
                                              Python sprawdza
                                              poprawność $TRK
```

> **UWAGA:** Wymaga 2 portów UART na RPi lub adaptera USB-UART,
> bo jeden port wysyła NMEA (symuluje GPS), drugi odbiera $TRK.

### Testy E2E (Faza 3+)

**Cel:** Sprawdzenie z prawdziwym modułem GPS.

- ATtiny + GPS na breadboardzie
- RPi jako odbiornik
- Weryfikacja: pozycja GPS bliska rzeczywistej lokalizacji
- Testy cold start, warm start, utrata fixu, ruch

### Narzędzia do debugowania

| Narzędzie | Zastosowanie |
|-----------|-------------|
| Analizator logiczny (np. Saleae Logic) | Podgląd sygnałów UART, timing |
| Oscyloskop | Weryfikacja poziomów napięć, baud rate |
| `avr-objdump -d firmware.elf` | Deasemblacja — analiza rozmiaru kodu |
| `avr-size firmware.elf` | Zużycie Flash, RAM, EEPROM |
| `tools/serial_monitor.py` | Podgląd danych UART w real-time |
| simavr | Symulator AVR — testy bez hardware (opcjonalnie) |

---

<a id="skille-claude"></a>
## Skille Claude do wgrywania i testowania

Poniżej zdefiniowane są „skille" — zestawy kroków, które Claude może wykonać
na poszczególnych etapach projektu, aby zbudować, wgrać i przetestować firmware.

### Skill: `build-firmware`

**Kiedy:** Po każdej zmianie kodu w `firmware/src/`

**Kroki:**
1. `cmake --preset avr-release` (konfiguracja)
2. `cmake --build --preset avr-release` (kompilacja)
3. Sprawdzenie wyniku `avr-size` — raport Flash/RAM usage
4. Weryfikacja: czy firmware mieści się w limitach MCU (2 KB Flash, 128 B RAM)
5. Artefakt: `build/firmware.hex`

**Oczekiwany output:**
```
   text    data     bss     dec     hex filename
   1842      12      45    1899     76b firmware.elf
Flash: 1854/2048 (90.5%)   RAM: 57/128 (44.5%)   ✓ OK
```

### Skill: `run-host-tests`

**Kiedy:** Po zmianach w parserze NMEA lub logice GPS

**Kroki:**
1. `cmake --preset host-test`
2. `cmake --build --preset host-test`
3. `ctest --preset host-test --output-on-failure`
4. Raport: ile testów passed/failed
5. W razie błędów — analiza i naprawa

### Skill: `flash-attiny`

**Kiedy:** Po udanym buildzie, gdy chcemy wgrać na hardware

**Kroki:**
1. Sprawdzenie czy `build/firmware.hex` istnieje i jest aktualny
2. Sprawdzenie połączenia z programatorem: `avrdude -c usbasp -p t2313 -v`
3. Wgranie: `avrdude -c usbasp -p t2313 -U flash:w:build/firmware.hex:i`
4. Weryfikacja: `avrdude -c usbasp -p t2313 -U flash:v:build/firmware.hex:i`
5. Raport sukcesu/błędu

**Parametry konfiguracyjne:**
- `PROGRAMMER`: usbasp | avrisp2 | arduino
- `PORT`: /dev/ttyUSB0 (dla arduino as ISP)
- `MCU`: t2313 | t841 | t1614

### Skill: `run-integration-tests`

**Kiedy:** Po wgraniu firmware na ATtiny, z podłączonym UART do RPi

**Kroki:**
1. Sprawdzenie czy port szeregowy dostępny (`/dev/ttyS0` lub `/dev/ttyUSB0`)
2. Uruchomienie: `pytest test/integration/ -v`
3. Testy:
   - Echo test (wysłanie → odbiór tego samego)
   - NMEA replay → weryfikacja wyjścia `$TRK`
   - Timeout test (brak danych → brak wyjścia)
4. Raport wyników

### Skill: `monitor-serial`

**Kiedy:** Debugowanie na żywo, podgląd co ATtiny wysyła

**Kroki:**
1. `python3 tools/serial_monitor.py --port /dev/ttyS0 --baud 9600`
2. Wyświetlanie odebranych danych w real-time
3. Opcja: logowanie do pliku z timestamp

### Skill: `check-resource-usage`

**Kiedy:** Optymalizacja, sprawdzanie czy firmware mieści się w MCU

**Kroki:**
1. `avr-size --mcu=attiny2313 -C build/firmware.elf`
2. Analiza: Flash usage %, RAM usage %
3. `avr-objdump -d build/firmware.elf | grep -c "^[0-9a-f]"` — liczba instrukcji
4. Identyfikacja największych funkcji: `avr-nm --size-sort build/firmware.elf`
5. Rekomendacje optymalizacji jeśli >90% zasobów

### Skill: `validate-nmea-replay`

**Kiedy:** Sprawdzanie parsera z nagranym NMEA bez prawdziwego GPS

**Kroki:**
1. Wysłanie `test/fixtures/sample_nmea.txt` przez UART do ATtiny
2. Odbiór i parsowanie wyjścia `$TRK` z ATtiny
3. Porównanie z oczekiwanymi wartościami (ground truth z pliku fixture)
4. Raport: ile zdań sparsowanych poprawnie / błędnie

### Skill: `full-pipeline`

**Kiedy:** Pełna weryfikacja po większych zmianach

**Kroki (sekwencyjnie):**
1. `build-firmware` → czy się kompiluje i mieści w MCU?
2. `run-host-tests` → czy logika parsera poprawna?
3. `check-resource-usage` → czy zasoby w normie?
4. `flash-attiny` → wgranie na hardware
5. `run-integration-tests` → czy działa na prawdziwym ATtiny?
6. Raport końcowy: ✓/✗ dla każdego kroku

---

## Harmonogram faz (bez estymacji czasowych)

| Faza | Zależności | Blokery |
|------|-----------|---------|
| Faza 0: Infrastruktura | — | Decyzja: build system (CMake vs PlatformIO) |
| Faza 1: Parser NMEA | Faza 0 (build system) | Decyzja: które zdania NMEA |
| Faza 2: Integracja GPS | Faza 1 + zakup modułu GPS | Hardware: moduł GPS, breadboard |
| Faza 3: Protokół + RPi | Faza 2 | Decyzja: format protokołu |
| Faza 4: Energooszczędność | Faza 3 | Decyzja: źródło zasilania, duty cycle |
| Faza 5: Rozszerzenia | Faza 3 (MVP gotowy) | Decyzja: kierunek rozwoju |

---

## Następne kroki (akcje do podjęcia)

1. **Rozstrzygnąć niedoprecyzowania** — patrz `NIEDOPRECYZOWANIA.md`, priorytet: MCU, GPS, metoda transmisji
2. **Zakupić komponenty** — minimum: moduł GPS (NEO-6M), programator USBasp (jeśli brak)
3. **Rozpocząć Fazę 0** — przywrócenie build system, reorganizacja katalogów
4. **Implementować parser NMEA** — można równolegle z zakupami hardware (testy hostowe nie wymagają sprzętu)
