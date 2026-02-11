# Plan implementacji: GPS Tracker na AVR + SIM800L

## Spis treści

1. [Fazy projektu — przegląd](#fazy-projektu)
2. [Faza 0 — Infrastruktura i toolchain](#faza-0)
3. [Faza 1 — Parser NMEA (testy bez hardware)](#faza-1)
4. [Faza 2 — Sterownik AT komend SIM800L (testy bez hardware)](#faza-2)
5. [Faza 3 — Integracja GPS (wymaga modułu GPS)](#faza-3)
6. [Faza 4 — HTTP POST przez SIM800L](#faza-4)
7. [Faza 5 — Integracja: GPS → HTTP (pełny tracker)](#faza-5)
8. [Faza 6 — Zarządzanie energią](#faza-6)
9. [Faza 7 — Rozszerzenia](#faza-7)
10. [Architektura sprzętu i połączenia](#architektura-sprzetu)
11. [Architektura testowania](#architektura-testowania)
12. [Skille Claude do budowania i testowania](#skille-claude)

---

<a id="fazy-projektu"></a>
## Fazy projektu — przegląd

```
Faza 0: Infrastruktura ──► Faza 1: Parser NMEA ──► Faza 2: Sterownik AT SIM800L
(build, struktura,          (state machine,          (AT komendy, HTTP POST,
 migracja na ATmega328P)     testy na hoście)         testy na hoście + HW)
          │                                                    │
          │                  ┌─────────────────────────────────┘
          │                  ▼
          ├──────────► Faza 3: Integracja GPS ──► Faza 4: HTTP POST
          │            (UART + NMEA parser        (GPS data → SIM800L
          │             na prawdziwym GPS)          → HTTP endpoint)
          │                                               │
          │                                               ▼
          │                                    Faza 5: Pełny tracker
          │                                    (GPS + SIM + main loop)
          │                                               │
          │                                               ▼
          └────────────────────────────────── Faza 6: Energooszczędność
                                             (sleep, MOSFET, duty cycle)
                                                          │
                                                          ▼
                                               Faza 7: Rozszerzenia
                                               (PCB, SIM7000, bufor offline)
```

**Kluczowa obserwacja:** Fazy 1 i 2 można realizować **równolegle** i **bez hardware
GPS** — na hoście (x86) lub z RPi symulującym wejście UART. To pozwala pisać
i testować firmware zanim dotrą zamówione moduły.

---

<a id="faza-0"></a>
## Faza 0 — Infrastruktura i toolchain

### Cel
Przygotować środowisko: build system, struktura katalogów, migracja z ATtiny2313
na ATmega328P, skrypty flashowania.

### Zadania

#### 0.1 Struktura katalogów

```
kma-avr-tracker/
├── docs/                          # Dokumentacja
│   ├── ZARYS_PROJEKTU.md
│   ├── PLAN_IMPLEMENTACJI.md
│   ├── NIEDOPRECYZOWANIA.md
│   ├── WYMAGANIA_GPS.md
│   └── WYMAGANIA_SPRZET.md
├── firmware/
│   ├── src/
│   │   ├── main.c                 # Punkt wejścia, main loop, duty cycle
│   │   ├── uart.c / uart.h        # Hardware UART (GPS)
│   │   ├── soft_uart.c / .h       # Software UART (SIM800L)
│   │   ├── nmea_parser.c / .h     # Parser NMEA (state machine)
│   │   ├── sim800l.c / sim800l.h  # Sterownik AT komend SIM800L
│   │   ├── http.c / http.h        # Warstwa HTTP (AT+HTTP...)
│   │   ├── gps.c / gps.h          # Logika GPS (fix, pozycja)
│   │   ├── led.c / led.h          # Sterowanie LED
│   │   ├── power.c / power.h      # Sleep mode, sterowanie MOSFET
│   │   └── config.h               # Piny, MCU, baud rate, APN, endpoint URL
│   ├── CMakeLists.txt
│   └── cmake/
│       └── avr-gcc-toolchain.cmake
├── test/
│   ├── host/                      # Testy na x86 (bez hardware)
│   │   ├── test_nmea_parser.c
│   │   ├── test_at_commands.c
│   │   ├── test_http_format.c
│   │   ├── mock_uart.c / .h       # Mock UART dla testów
│   │   └── CMakeLists.txt
│   ├── integration/               # Testy z hardware (RPi + ATmega + moduły)
│   │   ├── test_gps_nmea.py       # Replay NMEA → weryfikacja parsowania
│   │   ├── test_sim800l_at.py     # AT komendy → weryfikacja odpowiedzi
│   │   ├── test_http_post.py      # Pełny cykl HTTP POST
│   │   ├── conftest.py
│   │   └── requirements.txt
│   └── fixtures/
│       ├── sample_nmea.txt        # Nagrane zdania NMEA
│       └── sim800l_responses.txt  # Nagrane odpowiedzi AT SIM800L
├── tools/
│   ├── flash.sh                   # Flashowanie AVR
│   ├── serial_monitor.py          # Monitor UART
│   └── nmea_replay.py             # Symulator GPS (replay NMEA przez UART)
├── avr-app/                       # (legacy — zachowany dla odniesienia)
├── uart-test/                     # (legacy — zachowany dla odniesienia)
├── CMakeLists.txt                 # Root CMake
├── CMakePresets.json
└── .gitignore
```

#### 0.2 CMake + toolchain AVR

- Przywrócenie `cmake/avr-gcc-toolchain.cmake` (z historii git)
- Konfiguracja: `MCU=atmega328p`, `F_CPU=8000000UL`
- Presety: `avr-debug`, `avr-release`, `host-test`
- Target firmware: `firmware.hex`
- Target testy: `host-tests` (kompilacja gcc na x86)

#### 0.3 Software UART

- Implementacja software UART (bitbanging) dla komunikacji z SIM800L
- Bazując na istniejących bibliotekach AVR soft-UART
- Piny: np. PD4 (SW TX), PD5 (SW RX) — do konfiguracji
- Baud rate: 9600 (SIM800L domyślny)

#### 0.4 Skrypt flashowania

```bash
# tools/flash.sh
avrdude -c usbasp -p m328p -U flash:w:build/firmware.hex:i
```

### Kryteria ukończenia
- [ ] `cmake --build` produkuje `firmware.hex` dla ATmega328P
- [ ] `cmake --build --target host-tests` kompiluje i uruchamia testy x86
- [ ] Software UART nadaje/odbiera dane (weryfikacja oscyloskopem lub RPi)

---

<a id="faza-1"></a>
## Faza 1 — Parser NMEA

### Cel
Lekki, strumieniowy parser `$GPRMC` zoptymalizowany pod AVR.
**Nie wymaga modułu GPS** — testowany na hoście i z replay NMEA.

### Zadania

#### 1.1 Architektura parsera

**State machine (maszyna stanów):**
```
IDLE ──'$'──► HEADER ──','──► FIELDS ──'*'──► CHECKSUM ──'\n'──► DONE
  ▲                                                                │
  └────────────────────── reset ◄──────────────────────────────────┘
```

**Struktura danych:**
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

#### 1.2 Implementacja

- `nmea_parser.c / .h`
- Funkcja `nmea_feed_char(char c)` — podaje 1 znak
- Flaga `nmea_is_data_ready()` — czy nowy fix sparsowany
- `nmea_get_data(gps_data_t *out)` — kopiuje dane
- Weryfikacja checksum XOR
- Zero-copy, fixed-point (bez float)

#### 1.3 Testy hostowe

Przypadki testowe (kompilacja na x86, bez AVR):
- Poprawny `$GPRMC` → prawidłowa pozycja, czas, prędkość
- Nieprawidłowy checksum → odrzucenie
- Niekompletne zdanie → brak wyniku
- Zdania inne niż `$GPRMC` → ignorowane
- Śmieciowe dane między zdaniami → odporność
- Brak fixu (`V`) → `fix_valid = 'V'`
- Współrzędne S/W → ujemne wartości
- Pusty fix (brak pól) → zerowe dane, `fix_valid = 'V'`

#### 1.4 Optymalizacja

- Cel: parser < 1.5 KB Flash, < 50 B RAM
- Bez `printf`/`sprintf`/`sscanf`
- Własna konwersja ASCII→int (fixed-point)

### Kryteria ukończenia
- [ ] Wszystkie testy hostowe przechodzą
- [ ] `avr-size`: Flash < 1.5 KB, RAM < 50 B

---

<a id="faza-2"></a>
## Faza 2 — Sterownik AT komend SIM800L

### Cel
Zaimplementować warstwę komunikacji z SIM800L przez AT komendy.
**Można testować z posiadanym SIM800L bez modułu GPS.**

### Zadania

#### 2.1 Warstwa AT komend

```c
// sim800l.h
typedef enum {
    SIM_OK,
    SIM_ERROR,
    SIM_TIMEOUT,
    SIM_BUSY
} sim_result_t;

sim_result_t sim_init(void);              // AT, ATE0, AT+CPIN?
sim_result_t sim_check_network(void);     // AT+CREG?
sim_result_t sim_gprs_connect(const char* apn);  // AT+SAPBR
sim_result_t sim_http_get(const char* url);      // AT+HTTPINIT, HTTPPARA, HTTPACTION
sim_result_t sim_http_post(const char* url, const char* data);
sim_result_t sim_power_down(void);        // AT+CPOWD
```

#### 2.2 Sekwencja HTTP POST

```
AT              → OK
ATE0            → OK                  (wyłącz echo)
AT+CPIN?        → +CPIN: READY        (SIM OK)
AT+CREG?        → +CREG: 0,1          (zarejestrowany w sieci)
AT+SAPBR=3,1,"CONTYPE","GPRS"  → OK   (konfiguracja bearer)
AT+SAPBR=3,1,"APN","internet"  → OK   (APN operatora)
AT+SAPBR=1,1   → OK                  (otwórz bearer)
AT+HTTPINIT     → OK                  (inicjalizacja HTTP)
AT+HTTPPARA="CID",1             → OK
AT+HTTPPARA="URL","http://example.com/track?lat=51.1&lon=17.0&t=123456"  → OK
AT+HTTPACTION=0 → +HTTPACTION: 0,200,0  (HTTP GET, status 200)
AT+HTTPTERM     → OK                  (zamknij HTTP)
AT+SAPBR=0,1   → OK                  (zamknij bearer)
```

#### 2.3 Parsowanie odpowiedzi AT

- Bufor odpowiedzi ~64-128 B
- Szukanie "OK", "ERROR", "+CREG:", "+HTTPACTION:" etc.
- Timeout: konfigurowalny per komenda (np. HTTPACTION do 30s)
- State machine: SEND → WAIT_RESPONSE → PARSE → DONE/ERROR

#### 2.4 Testy

**Na hoście (x86):**
- Symulacja odpowiedzi AT z pliku fixture
- Weryfikacja kolejności komend AT
- Testowanie timeout i retry

**Na hardware (RPi + SIM800L):**
- RPi wysyła AT komendy przez UART do SIM800L (bezpośrednio, bez MCU)
- Weryfikacja: rejestracja w sieci, GPRS, HTTP GET/POST
- Nagrywanie odpowiedzi do `test/fixtures/sim800l_responses.txt`

**Na hardware (ATmega + SIM800L):**
- ATmega wysyła AT przez software UART
- RPi monitoruje hardware UART (debug log)
- Weryfikacja pełnego cyklu HTTP

### Kryteria ukończenia
- [ ] SIM800L rejestruje się w sieci (AT+CREG → 0,1)
- [ ] HTTP GET na testowy URL zwraca status 200
- [ ] Testy hostowe parsowania AT response przechodzą
- [ ] Flash < 4 KB, RAM < 200 B

---

<a id="faza-3"></a>
## Faza 3 — Integracja GPS

### Cel
Podłączyć moduł GPS do ATmega328P, odebrać NMEA, sparsować pozycję.
**Wymaga zakupionego modułu GPS.**

### Zadania

#### 3.1 Podłączenie GPS do hardware UART

```
GPS TX ──► ATmega328P PD0 (HW UART RX)
GPS RX ◄── ATmega328P PD1 (HW UART TX)  ← opcjonalnie konfiguracja GPS
GPS VCC ── 3.3V (przez MOSFET lub bezpośrednio)
GPS GND ── GND
```

#### 3.2 Integracja parser + UART

- ISR(USART_RX_vect) → `nmea_feed_char(UDR0)`
- Main loop sprawdza `nmea_is_data_ready()`
- Odczyt danych → `gps_data_t`
- LED PD2: blink = szukanie fixu, ciągły = fix OK

#### 3.3 Test na hardware

- Flashowanie firmware na ATmega328P
- GPS z anteną na zewnątrz (widok nieba)
- RPi podłączony do debug UART → podgląd sparsowanych danych
- Weryfikacja: pozycja ~zgodna z rzeczywistą lokalizacją

### Kryteria ukończenia
- [ ] ATmega odbiera NMEA z GPS
- [ ] Parser poprawnie wyodrębnia lat, lon, time, speed
- [ ] LED sygnalizuje GPS fix
- [ ] Pozycja zgadza się z rzeczywistą (±100m)

---

<a id="faza-4"></a>
## Faza 4 — HTTP POST z danymi GPS

### Cel
Połączyć parser NMEA z HTTP POST — wysłać pozycję na endpoint.

### Zadania

#### 4.1 Formatowanie URL

```c
// Formatowanie bez sprintf — własna konwersja fixed-point → ASCII
// Przykład URL:
// http://example.com/track?lat=51.10728&lon=17.03864&time=123456&spd=52&fix=A
void format_tracking_url(char* buf, size_t len, const gps_data_t* data);
```

#### 4.2 Sekwencja główna

```c
while(1) {
    // 1. Czekaj na GPS fix
    while(!nmea_is_data_ready()) { /* czekaj */ }
    gps_data_t pos;
    nmea_get_data(&pos);

    // 2. Wyślij HTTP
    if(pos.fix_valid == 'A') {
        char url[128];
        format_tracking_url(url, sizeof(url), &pos);
        sim_http_get(url);
        led_activity_blink();
    }

    // 3. Czekaj interwał
    delay_seconds(REPORT_INTERVAL);
}
```

#### 4.3 Test end-to-end

- ATmega + GPS + SIM800L na breadboardzie
- GPS fix → HTTP POST → sprawdzenie na serwerze
- RPi jako debug monitor (opcjonalnie)

### Kryteria ukończenia
- [ ] Pozycja GPS ląduje na HTTP endpoint
- [ ] Interwał raportowania działa prawidłowo
- [ ] Retry przy braku zasięgu (min. 3 próby)

---

<a id="faza-5"></a>
## Faza 5 — Pełny tracker (integracja)

### Cel
Połączyć wszystko w stabilny, działający tracker.

### Zadania

#### 5.1 Main loop (pełny)

```c
int main(void) {
    init_uart();          // HW UART dla GPS
    init_soft_uart();     // SW UART dla SIM800L
    init_leds();
    init_nmea_parser();
    sim_init();           // Inicjalizacja SIM800L
    sim_gprs_connect(APN);

    led_startup_sequence();

    while(1) {
        // 1. Czekaj na GPS fix (z timeout)
        uint8_t fix_ok = wait_for_gps_fix(GPS_TIMEOUT_S);

        // 2. Wyślij jeśli jest fix
        if(fix_ok) {
            gps_data_t pos;
            nmea_get_data(&pos);
            send_position_http(&pos);
        }

        // 3. Czekaj do następnego cyklu
        delay_seconds(REPORT_INTERVAL);
    }
}
```

#### 5.2 Error handling

- GPS timeout (brak fixu po N sekund) → spróbuj ponownie / raportuj błąd
- SIM800L nie odpowiada → reset (pin RESET), reinicjalizacja
- HTTP error → retry z backoff
- Brak zasięgu GSM → czekaj / buforuj pozycję

#### 5.3 Konfiguracja (`config.h`)

```c
#define F_CPU         8000000UL
#define GPS_BAUD      9600
#define SIM_BAUD      9600
#define REPORT_INTERVAL_S  300  // 5 minut
#define GPS_TIMEOUT_S      120  // 2 minuty max na fix

#define APN           "internet"
#define ENDPOINT_URL  "http://example.com/track"

// Piny
#define LED_FIX_PIN   PD2
#define LED_ACT_PIN   PD3
#define SIM_TX_PIN    PD4  // Software UART
#define SIM_RX_PIN    PD5  // Software UART
#define GPS_PWR_PIN   PD6  // MOSFET sterowanie zasilaniem GPS
```

### Kryteria ukończenia
- [ ] Tracker działa autonomicznie (bez RPi)
- [ ] Pozycja co N minut na serwerze
- [ ] Obsługa błędów (timeout GPS, brak zasięgu, reset SIM)
- [ ] Flash < 16 KB, RAM < 1 KB

---

<a id="faza-6"></a>
## Faza 6 — Zarządzanie energią

### Cel
Zminimalizować pobór prądu dla pracy bateryjnej.

### Zadania

#### 6.1 Sleep mode ATmega328P

- Power-down mode: ~0.1 µA (wyłączony BOD)
- Budzenie: Watchdog Timer (max 8s per cykl, łączymy w dłuższe interwały)
- Sekwencja: sleep → WDT interrupt → counter++ → jeśli N*8s ≥ interwał → wake

#### 6.2 Sterowanie zasilaniem GPS

- MOSFET N-ch (2N7000) na GND modułu GPS
- Pin GPIO steruje gate → HIGH = GPS włączony, LOW = GPS wyłączony
- Po włączeniu: czekaj na warm start (1-5s z backup battery) lub cold start (30-60s)

#### 6.3 SIM800L sleep mode

- `AT+CSCLK=1` — włącz auto sleep (reaguje na DTR pin)
- DTR HIGH → SIM800L śpi (~1 mA)
- DTR LOW → SIM800L budzi się
- Lub: `AT+CPOWD=1` → pełne wyłączenie (~60 µA), ale wymaga ponownej inicjalizacji

#### 6.4 Budżet energetyczny

| Faza cyklu | Czas | Prąd | Energia |
|-----------|------|------|---------|
| Sleep (MCU + SIM sleep + GPS off) | ~290s | ~1.2 mA | 348 mAs |
| GPS warm start | ~5s | ~50 mA | 250 mAs |
| Parsowanie NMEA | ~1s | ~5 mA | 5 mAs |
| SIM800L wake + HTTP | ~10s | ~200 mA avg | 2000 mAs |
| **Razem per cykl (5 min)** | 300s | — | **~2603 mAs** |
| **Średni prąd** | — | **~8.7 mA** | — |

**Czas pracy:** LiPo 2000 mAh / 8.7 mA ≈ **~230h ≈ ~9.5 dnia**

### Kryteria ukończenia
- [ ] Sleep current < 2 mA (MCU + SIM sleep + GPS off)
- [ ] Cykl 5 min działa stabilnie na baterii
- [ ] Czas pracy na LiPo 2000mAh > 7 dni (mierzone)

---

<a id="faza-7"></a>
## Faza 7 — Rozszerzenia (po MVP)

| Rozszerzenie | Opis | Złożoność |
|-------------|-------|-----------|
| Pomiar baterii | ADC na dzielniku napięcia LiPo, raportowanie % w HTTP | Niska |
| Bufor offline | Zapis do EEPROM (1 KB = ~50 pozycji) przy braku zasięgu | Średnia |
| Migracja na SIM7000G | LTE Cat-M + GPS wbudowany, eliminacja osobnego GPS | Średnia |
| OTA config | Odczyt odpowiedzi HTTP → zmiana interwału, reset, etc. | Średnia |
| PCB | Projektowanie w KiCad, zamówienie u producenta | Wysoka |
| Geofencing | Alarm przy wyjściu ze strefy (Haversine distance) | Średnia |
| Dashboard webowy | Mapa (Leaflet.js) + historia tras + API | Wysoka |

---

<a id="architektura-sprzetu"></a>
## Architektura sprzętu i połączenia

### Pinout ATmega328P (docelowy)

```
                        ATmega328P-PU (DIP-28)
                      ┌────────────────────────┐
           RESET ────┤1  PC6        PC5    28├──── (wolny / SCL)
      (HW RX ← GPS) ┤2  PD0        PC4    27├──── (wolny / SDA)
      (HW TX → GPS)  ┤3  PD1        PC3    26├──── (wolny / ADC3)
          LED fix ───┤4  PD2        PC2    25├──── (wolny / ADC2)
        LED activity ┤5  PD3        PC1    24├──── (wolny / ADC1)
    (SW TX → SIM800L)┤6  PD4        PC0    23├──── ADC0 ← pomiar baterii
    (SW RX ← SIM800L)┤7  PD5        GND    22├──── GND
       GPS PWR MOSFET┤8  PD6        AREF   21├──── (wolny)
     SIM DTR / RESET ┤9  PD7        AVCC   20├──── VCC
              (XTAL1)┤10 PB6        PB5    19├──── SCK (ISP)
              (XTAL2)┤11 PB7        PB4    18├──── MISO (ISP)
                     ┤12 PD5(nota)  PB3    17├──── MOSI (ISP)
                     ┤13 PD6(nota)  PB2    16├──── (wolny / SS)
                 GND ┤14 GND       PB1    15├──── (wolny)
                     ┤15 VCC       PB0    14├──── (wolny)
                      └────────────────────────┘
```

> Nota: piny 12-13 to kontynuacja PB — schematy DIP-28 różnią się od opisu.
> Dokładne przypisania zweryfikować z datasheetem ATmega328P.

### Schemat połączeń

```
                         LiPo 3.7V
                            │
                     ┌──────┼───────────────────────────────┐
                     │      │                               │
                ┌────┴────┐ │  ┌──────┐              ┌─────┴──────┐
                │ 1000µF  │ │  │100nF │              │   SIM800L   │
                └────┬────┘ │  └──┬───┘              │             │
                     │      │     │                  │  VCC ◄──────┤
                     │   ┌──┴─────┴──┐               │  GND ◄───┐ │
                     │   │           │               │  TX ─────┼─┼──► PD5 (SW RX)
                     │   │ ATmega328P│               │  RX ◄────┼─┼─── PD4 (SW TX)
                     │   │           │               │  DTR ◄───┼─┼─── PD7
                     │   │ VCC ◄─────┤               │  RST     │ │
                     │   │ GND ◄───┐ │               └──────────┘ │
                     │   │         │ │                            │
                     │   │ PD0(RX)◄┼─┼──── GPS TX                │
                     │   │ PD1(TX)─┼─┼───► GPS RX (opcja)        │
                     │   │         │ │                            │
                     │   │ PD2 ────┼─┼──── [330Ω]──LED(fix)──GND │
                     │   │ PD3 ────┼─┼──── [330Ω]──LED(act)──GND │
                     │   │ PD6 ────┼─┼──── MOSFET gate            │
                     │   │         │ │       │                    │
                     │   │ PB5(SCK)│ │     drain── GPS GND        │
                     │   │ PB4(MISO│ │     source── GND           │
                     │   │ PB3(MOSI│ │                            │
                     │   └─────────┘ │  GPS Module                │
                     │               │  ┌──────────┐              │
                     │               │  │ VCC ◄────┤── 3.7V      │
                     │               │  │ GND ◄────┤── via MOSFET│
                     │               │  │ TX ──────┤──► PD0      │
                     │               │  │ RX ◄─────┤──  PD1      │
                     │               │  └──────────┘              │
                     │               │                            │
                     └───────────────┴────────────────────────────┘
                                    GND (wspólny)
```

---

<a id="architektura-testowania"></a>
## Architektura testowania

### Piramida testów

```
              ┌───────────────────────┐
              │    Testy E2E          │  ← GPS + ATmega + SIM800L + serwer
              │  (pełny hardware)     │     Ręczne: pozycja na mapie
              ├───────────────────────┤
              │ Testy integracyjne    │  ← RPi + ATmega + moduły (pojedyncze)
              │ (hardware-in-loop)    │     Automatyczne: replay NMEA, AT mock
              ├───────────────────────┤
              │ Testy hostowe         │  ← Kompilacja na x86 (gcc)
              │ (unit tests)          │     Parser NMEA, AT response parsing,
              │                       │     URL formatting — szybkie, bez HW
              └───────────────────────┘
```

### Stanowisko testowe (RPi jako narzędzie)

**Konfiguracja 1: Test parsera NMEA (bez GPS)**
```
RPi USB-UART TX ──NMEA replay──► ATmega HW UART RX (PD0)
                                  ATmega debug TX ────► RPi USB-UART RX (drugi adapter)
```
RPi wysyła nagrane NMEA, ATmega parsuje, RPi weryfikuje wyjście.

**Konfiguracja 2: Test SIM800L (bez GPS)**
```
ATmega SW UART TX (PD4) ──► SIM800L RX
ATmega SW UART RX (PD5) ◄── SIM800L TX
RPi ── monitor UART ────────► ATmega HW UART TX (debug log)
```

**Konfiguracja 3: Pełny test E2E**
```
GPS ──► ATmega ──► SIM800L ──► HTTP serwer
               └──► RPi (debug monitor, opcja)
```

### Replay NMEA (symulator GPS)

```python
# tools/nmea_replay.py
import serial, time

def replay(port="/dev/ttyUSB0", baud=9600, file="test/fixtures/sample_nmea.txt"):
    ser = serial.Serial(port, baud)
    with open(file) as f:
        for line in f:
            ser.write(line.strip().encode() + b'\r\n')
            time.sleep(0.1)
    ser.close()
```

### Fixture NMEA

```
$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
$GPRMC,123520,A,4807.039,N,01131.001,E,022.5,084.5,230394,003.1,W*6E
$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*47
$GPRMC,123521,V,,,,,,,230394,003.1,W*6D
```

---

<a id="skille-claude"></a>
## Skille Claude do budowania i testowania

Zestawy kroków, które Claude może wykonać na poszczególnych etapach
aby zbudować, wgrać i przetestować firmware.

### Skill: `build-firmware`

**Kiedy:** Po każdej zmianie kodu w `firmware/src/`
```
1. cmake --preset avr-release
2. cmake --build --preset avr-release
3. avr-size --mcu=atmega328p -C build/firmware.elf
4. Weryfikacja: Flash < 32KB, RAM < 2KB
5. Artefakt: build/firmware.hex
```

### Skill: `run-host-tests`

**Kiedy:** Po zmianach w parserze NMEA, AT komendach, formatowaniu URL
```
1. cmake --preset host-test
2. cmake --build --preset host-test
3. ctest --preset host-test --output-on-failure
4. Raport: passed/failed
```

### Skill: `flash-avr`

**Kiedy:** Po udanym buildzie, wgranie na hardware
```
1. Sprawdź: build/firmware.hex istnieje
2. avrdude -c usbasp -p m328p -v                          # test połączenia
3. avrdude -c usbasp -p m328p -U flash:w:build/firmware.hex:i  # flash
4. avrdude -c usbasp -p m328p -U flash:v:build/firmware.hex:i  # verify
```
Parametry: `PROGRAMMER=usbasp|arduino`, `PORT=/dev/ttyUSB0`

### Skill: `test-nmea-replay`

**Kiedy:** Weryfikacja parsera z symulowanym GPS (RPi + ATmega)
```
1. Sprawdź porty: /dev/ttyUSB0 (replay), /dev/ttyUSB1 (odbiór)
2. python3 tools/nmea_replay.py --port /dev/ttyUSB0 --file test/fixtures/sample_nmea.txt
3. python3 tools/serial_monitor.py --port /dev/ttyUSB1 --timeout 10
4. Porównaj wyjście z oczekiwanymi danymi
```

### Skill: `test-sim800l`

**Kiedy:** Weryfikacja komunikacji z SIM800L
```
1. Sprawdź port: /dev/ttyUSB0
2. pytest test/integration/test_sim800l_at.py -v
3. Testy: rejestracja sieci, GPRS connect, HTTP GET na testowy URL
```

### Skill: `test-full-e2e`

**Kiedy:** Pełny cykl: GPS → parse → HTTP POST
```
1. Sprawdź: ATmega flashowany, GPS podłączony, SIM800L z kartą SIM
2. python3 tools/serial_monitor.py --port /dev/ttyUSB0 --log output.log &
3. Czekaj na GPS fix (obserwuj LED)
4. Sprawdź endpoint HTTP czy pozycja dotarła
5. Porównaj pozycję z rzeczywistą lokalizacją
```

### Skill: `check-resources`

**Kiedy:** Optymalizacja, sprawdzanie limitów MCU
```
1. avr-size --mcu=atmega328p -C build/firmware.elf
2. avr-nm --size-sort -r build/firmware.elf | head -20  # największe symbole
3. Raport: Flash %, RAM %, top 5 funkcji po rozmiarze
```

### Skill: `full-pipeline`

**Kiedy:** Pełna weryfikacja po większych zmianach
```
1. build-firmware    → kompilacja OK?
2. run-host-tests    → logika OK?
3. check-resources   → mieści się w MCU?
4. flash-avr         → wgranie OK?
5. test-nmea-replay  → parser OK na hardware?
6. test-sim800l      → komunikacja GSM OK?
7. test-full-e2e     → pełny cykl OK?
```

---

## Harmonogram faz

| Faza | Zależność od hardware | Można robić teraz? |
|------|----------------------|-------------------|
| Faza 0: Infrastruktura | Nie | **Tak** — cmake, struktura, SW UART |
| Faza 1: Parser NMEA | Nie | **Tak** — testy na x86 |
| Faza 2: Sterownik AT SIM800L | SIM800L (posiadany) | **Tak** — testy na hoście + hardware |
| Faza 3: Integracja GPS | Moduł GPS (do zakupu) | Nie — czekamy na dostawę |
| Faza 4: HTTP POST | GPS + SIM800L | Nie |
| Faza 5: Pełny tracker | GPS + SIM800L + ATmega | Nie |
| Faza 6: Energooszczędność | Pełny tracker | Nie |
| Faza 7: Rozszerzenia | Zależy | Nie |

**Wniosek:** Fazy 0, 1 i 2 można realizować równolegle i natychmiast,
bez czekania na moduł GPS. To ~60% kodu firmware.

---

## Następne kroki

1. **Rozstrzygnąć niedoprecyzowania** — priorytet: MCU, endpoint HTTP, zasilanie
2. **Zamówić sprzęt** — patrz `WYMAGANIA_SPRZET.md`
3. **Faza 0** — build system, struktura katalogów
4. **Faza 1 + 2 równolegle** — parser NMEA + sterownik SIM800L (bez czekania na GPS)
5. **Po dostarczeniu GPS** — Faza 3, 4, 5 sekwencyjnie
