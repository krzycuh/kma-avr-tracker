# Projekt układu: ATtiny2313 + SIM800L (+ RPi do programowania i testów)

Okablowanie bazuje na schemacie referencyjnym
[mcore1976/gpstracker](https://github.com/mcore1976/gpstracker) oraz na Twoim istniejącym
połączeniu RPi↔AVR (programowanie linuxgpio + UART). W wersji REST **linia RI/RING nie
jest używana** (nie czekamy na dzwonek) — układ jest prostszy niż w projekcie referencyjnym.

## 1. Wykorzystane piny ATtiny2313 (PDIP-20)

```
                    ATtiny2313
                  ┌─────∪─────┐
   RESET (RPi) ──┤1  PA2  VCC 20├── zasilanie (3,3–4,5 V)
   RXD  ← TXD ───┤2  PD0  PB7 19├── SCK  (RPi GPIO11 - programowanie)
   TXD  → RXD ───┤3  PD1  PB6 18├── MISO/DO (RPi GPIO9 - programowanie)
                 ┤4  PA1  PB5 17├── MOSI/DI (RPi GPIO10 - programowanie)
                 ┤5  PA0  PB4 16├
                 ┤6  PD2  PB3 15├
   LED statusu ──┤7  PD3  PB2 14├
                 ┤8  PD4  PB1 13├
                 ┤9  PD5  PB0 12├
   GND ──────────┤10 GND  PD6 11├
                  └─────────────┘
```

| Pin AVR | Sygnał | Programowanie (RPi, BCM) | Runtime — testy (RPi) | Runtime — tracker (SIM800L) |
|---|---|---|---|---|
| 1 (PA2) | RESET | GPIO4 | — | podciągnięcie 10 kΩ do VCC |
| 2 (PD0) | UART RXD | — | GPIO14 (TXD) | TXD modułu |
| 3 (PD1) | UART TXD | — | GPIO15 (RXD) | RXD modułu |
| 6 (PD2) | wolny | — | dioda LED (testy) | wolny |
| 7 (PD3) | dioda LED | — | dioda LED (testy) | dioda statusu (miga po raporcie) |
| 17 (PB5) | MOSI/DI | GPIO10 | — | — |
| 18 (PB6) | MISO/DO | GPIO9 | — | — |
| 19 (PB7) | SCK | GPIO11 | — | — |
| 20 (VCC) | zasilanie | 3,3 V z RPi | 3,3 V z RPi | z sekcji zasilania (~4 V) |
| 10 (GND) | masa | wspólna z RPi | wspólna z RPi | wspólna z SIM800L |

**Uwagi:**
- Zegar: wewnętrzny RC 8 MHz z dzielnikiem /8 = 1 MHz — **fabryczne fuse bity**
  (`lfuse=0x64`, `hfuse=0xdf`), niczego nie trzeba przepalać.
- Przy testach z RPi zasilaj AVR z 3,3 V (piny UART RPi nie tolerują 5 V!).
- Diody LED z Twojej płytki testowej (PD2/PD3) mogą zostać — w trackerze PD3 pełni rolę
  diody statusu, PD2 jest nieużywane.

## 2. Połączenie ATtiny2313 ↔ SIM800L (wersja docelowa)

```
   zasilanie ~4V ──┬──────────────────────┬─────────────┐
                   │                      │             │
              ┌────┴────┐            1000µF²         ┌──┴──────────┐
              │ ATtiny  │             + 100nF        │  SIM800L    │
              │  2313   │                │           │             │
   10kΩ ──────┤ RESET   │               GND          │  ANT ═╗     │
   (do VCC)   │         │                            │       ║     │
              │ PD0 (2) ├──────────────◄─────────────┤ TXD         │
              │ PD1 (3) ├──────────────►────────────*┤ RXD         │
              │ PD3 (7) ├──[LED]──[470Ω]──GND        │ RI  (n/c)   │
              │ GND(10) ├────────────────────────────┤ GND         │
              └─────────┘        (gruby przewód!)    │ VCC         │
                                                     └──┬──────────┘
                                                        │
                              zasilanie ~4V ────────────┘ (gruby przewód!)
```

- `*` linia PD1→RXD: logika SIM800L to 2,8 V. Przy zasilaniu AVR z 3,3–3,7 V połączenie
  bezpośrednie działa (tak robi projekt referencyjny). Przy VCC AVR ≥ 4 V daj **dzielnik
  rezystorowy** na tej linii (np. szeregowo 4,7 kΩ, do GND 10 kΩ). Kierunek TXD→PD0
  jest bezpieczny bez zmian.
- `²` kondensator 1000 µF/16 V **jak najbliżej pinów VCC/GND modułu** + 100 nF ceramiczny
  równolegle. Bez tego moduł resetuje się przy impulsach nadawania (do 2 A!).
- Pin RI modułu zostaje **niepodłączony** (w tej architekturze niepotrzebny).
- Karta SIM: nano/micro wg gniazda modułu, z pakietem danych GPRS,
  **z wyłączonym kodem PIN** (albo PIN 1111 — patrz `SIM_PIN` w `gps-tracker.c`).

## 3. Sekcja zasilania (warianty z projektu referencyjnego)

SIM800L wymaga **3,4–4,4 V** (bezwzględnie < 5 V!) i impulsów prądu do **2 A**.

**Wariant A - 3×AA (zalecany na start, najprostszy):**
```
3×AA (4,5V świeże) ──┬── VCC ATtiny2313
                     └── VCC SIM800L (+ 1000µF + 100nF przy module)
```
Baterie alkaliczne bezpośrednio, bez żadnych diod. Nie używaj akumulatorków Ni-MH
(3×1,2 V = 3,6 V — zadziała, ale krócej). Uwaga: przy cyklicznym raportowaniu GPRS
zużycie jest większe niż w wersji „na żądanie" — czas pracy zależy od
`REPORT_INTERVAL_MIN` (pomiar w kamieniu M6).

**Wariant B - powerbank USB 5 V:**
5 V przez **1 diodę 1N4007** (spadek ~0,7 V) → ~4,3 V. Najtańszy powerbank bez czujnika
poboru prądu (inteligentne wyłączają się przy poborze < 50 mA... choć przy regularnym
ruchu GPRS zwykle zostają aktywne).

**Wariant C - instalacja auta/motocykla 12 V:**
12 V → LM7805 (z radiatorem) → 5 V → 1×1N4007 → ~4,3 V. Albo przetwornica step-down
(LM2596) ustawiona na 4,0–4,2 V — mniej strat ciepła. Dla trackera raportującego cyklicznie
to najwygodniejszy wariant (brak troski o baterię).

We wszystkich wariantach: grube przewody / szerokie ścieżki GND i VCC do modułu,
masa AVR i SIM800L połączona możliwie krótko.

## 4. Stanowisko testowe M1–M2 (RPi jako symulator SIM800L)

Dokładnie to samo okablowanie, którego już używasz do testów UART:

| RPi (BCM) | ATtiny2313 | Rola |
|---|---|---|
| GPIO14 (TXD) | pin 2 (PD0/RXD) | RPi „udaje" TXD modułu |
| GPIO15 (RXD) | pin 3 (PD1/TXD) | odbiór komend AT z AVR |
| 3,3 V | pin 20 (VCC) | zasilanie AVR na czas testów |
| GND | pin 10 (GND) | wspólna masa |

Uruchomienie: `python3 uart-test/sim800l-simulator.py`. Pamiętaj o zwolnieniu portu
`/dev/ttyS0` (konsola szeregowa RPi wyłączona — jak przy dotychczasowych testach).
Programowanie (linuxgpio) i symulator działają naprzemiennie bez przepinania.

## 5. Podsłuch komunikacji AVR↔SIM800L (debug w M5)

Przy integracji z prawdziwym modułem możesz podpiąć **tylko RXD RPi (GPIO15)** do linii
TXD AVR (pin 3) albo TXD modułu — i czytać na RPi całą konwersację
(`minicom -D /dev/ttyS0 -b 9600`). Wspólna masa wymagana; TXD RPi zostaw odłączone.

## 6. Lista zakupów (BOM)

| Element | Ilość | Uwagi |
|---|---|---|
| SIM800L (płytka z gniazdem SIM i anteną) | 1 | wersja „niebieska" z pinami VCC/GND/TXD/RXD |
| Karta SIM (nano/micro) | 1 | taryfa z pakietem danych GPRS, PIN wyłączony |
| Kondensator elektrolityczny 1000 µF/16 V | 1–2 | przy VCC modułu |
| Kondensator ceramiczny 100 nF | 1–2 | przy VCC modułu i AVR |
| Rezystor 10 kΩ | 2 | podciąganie RESET; ew. dzielnik RXD |
| Rezystor 4,7 kΩ | 1 | ew. dzielnik RXD |
| Rezystor 470 Ω + LED | 1 | dioda statusu na PD3 (opcjonalna) |
| Dioda 1N4007 | 0–6 | zależnie od wariantu zasilania |
| Koszyk 3×AA | 1 | wariant A |
| Płytka stykowa → uniwersalna | 1+1 | prototyp → montaż docelowy |

(ATtiny2313, RPi i przewody już masz. Po stronie serwera: dowolny VPS/domowy serwer
z publicznym adresem — patrz `docs/PLAN.md`, kamień M3.)
