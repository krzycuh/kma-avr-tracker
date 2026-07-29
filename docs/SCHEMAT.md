# Projekt układu: ATtiny2313 + SIM800L (+ RPi do programowania i testów)

Bazuje na schemacie referencyjnym
[GPS-locator-ATTINY2313-4313-SIM800L-version.png](https://github.com/mcore1976/gpstracker/blob/master/GPS-locator-ATTINY2313-4313-SIM800L-version.png)
oraz na Twoim istniejącym okablowaniu RPi↔AVR (programowanie linuxgpio + UART).

## 1. Wykorzystane piny ATtiny2313 (PDIP-20)

```
                    ATtiny2313
                  ┌─────∪─────┐
   RESET (RPi) ──┤1  PA2  VCC 20├── zasilanie (3,3–4,5 V)
   RXD  ← TXD ───┤2  PD0  PB7 19├── SCK  (RPi GPIO11 - programowanie)
   TXD  → RXD ───┤3  PD1  PB6 18├── MISO/DO (RPi GPIO9 - programowanie)
                 ┤4  PA1  PB5 17├── MOSI/DI (RPi GPIO10 - programowanie)
                 ┤5  PA0  PB4 16├
   RI (SIM800L)──┤6  PD2  PB3 15├
                 ┤7  PD3  PB2 14├
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
| 6 (PD2) | wejście RI | — | dioda LED (testy) / GPIO symulatora | RI/RING modułu |
| 7 (PD3) | dioda LED | — | dioda LED (testy) | wolny |
| 17 (PB5) | MOSI/DI | GPIO10 | — | — |
| 18 (PB6) | MISO/DO | GPIO9 | — | — |
| 19 (PB7) | SCK | GPIO11 | — | — |
| 20 (VCC) | zasilanie | 3,3 V z RPi | 3,3 V z RPi | z sekcji zasilania (~4 V) |
| 10 (GND) | masa | wspólna z RPi | wspólna z RPi | wspólna z SIM800L |

**Uwagi:**
- Zegar: wewnętrzny RC 8 MHz z dzielnikiem /8 = 1 MHz — **fabryczne fuse bity**
  (`lfuse=0x64`, `hfuse=0xdf`), niczego nie trzeba przepalać.
- W wersji trackera **na PD2 nie może wisieć dioda LED** — to wejście RI z podciąganiem;
  dioda zafałszuje poziom logiczny.
- Przy testach z RPi zasilaj AVR z 3,3 V (piny UART RPi nie tolerują 5 V!).

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
              │ PD2 (6) ├──────────────◄─────────────┤ RI (RING)   │
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
- RI w spoczynku: stan wysoki (2,8 V); przy połączeniu przychodzącym: stan niski.
  Firmware ma włączone wewnętrzne podciąganie na PD2. 2,8 V to niewiele powyżej progu
  „1" przy VCC=4,5 V — jeśli PD2 odczytuje śmieci, zasil AVR niżej (3,3–3,7 V)
  albo dodaj zewnętrzne podciąganie 100 kΩ do 2,8 V (pin VDD_EXT modułu).
- Karta SIM: nano/micro wg gniazda modułu, **z wyłączonym kodem PIN** (albo PIN 1111 —
  patrz `SIM_PIN` w `gps-tracker.c`).

## 3. Sekcja zasilania (warianty z projektu referencyjnego)

SIM800L wymaga **3,4–4,4 V** (bezwzględnie < 5 V!) i impulsów prądu do **2 A**.

**Wariant A - 3×AA (zalecany na start, najprostszy):**
```
3×AA (4,5V świeże) ──┬── VCC ATtiny2313
                     └── VCC SIM800L (+ 1000µF + 100nF przy module)
```
Baterie alkaliczne bezpośrednio, bez żadnych diod. ~2 tygodnie pracy. Nie używaj
akumulatorków Ni-MH (3×1,2 V = 3,6 V — zadziała, ale krócej).

**Wariant B - powerbank USB 5 V:**
5 V przez **1 diodę 1N4007** (spadek ~0,7 V) → ~4,3 V. Najtańszy powerbank bez czujnika
poboru prądu (inteligentne wyłączają się przy poborze < 50 mA).

**Wariant C - instalacja auta/motocykla 12 V:**
12 V → LM7805 (z radiatorem) → 5 V → 1×1N4007 → ~4,3 V. Albo przetwornica step-down
(LM2596) ustawiona na 4,0–4,2 V — mniej strat ciepła.

We wszystkich wariantach: grube przewody / szerokie ścieżki GND i VCC do modułu,
masa AVR i SIM800L połączona możliwie krótko.

## 4. Stanowisko testowe M1–M2 (RPi jako symulator SIM800L)

To samo okablowanie, którego już używasz do testów UART, plus jedna linia na RI:

| RPi (BCM) | ATtiny2313 | Rola |
|---|---|---|
| GPIO14 (TXD) | pin 2 (PD0/RXD) | RPi „udaje" TXD modułu |
| GPIO15 (RXD) | pin 3 (PD1/TXD) | odbiór komend AT z AVR |
| GPIO17 (dowolny wolny) | pin 6 (PD2) | symulacja linii RI (`--ri-pin 17`) |
| 3,3 V | pin 20 (VCC) | zasilanie AVR na czas testów |
| GND | pin 10 (GND) | wspólna masa |

Uruchomienie: `python3 uart-test/sim800l-simulator.py --ri-pin 17`, potem komenda `ring`.
Pamiętaj o zwolnieniu portu `/dev/ttyS0` (konsola szeregowa RPi wyłączona — jak przy
dotychczasowych testach) i o tym, że programowanie (linuxgpio) i symulator mogą działać
naprzemiennie bez przepinania.

## 5. Podsłuch komunikacji AVR↔SIM800L (debug w M5)

Przy integracji z prawdziwym modułem możesz podpiąć **tylko RXD RPi (GPIO15)** do linii
TXD AVR (pin 3) albo TXD modułu — i czytać na RPi całą konwersację
(`minicom -D /dev/ttyS0 -b 9600`). Wspólna masa wymagana; TXD RPi zostaw odłączone.

## 6. Lista zakupów (BOM)

| Element | Ilość | Uwagi |
|---|---|---|
| SIM800L (płytka z gniazdem SIM i anteną) | 1 | wersja „niebieska" z pinami VCC/GND/TXD/RXD/RST/RI |
| Karta SIM (nano/micro) | 1 | taryfa z SMS + GPRS, PIN wyłączony |
| Kondensator elektrolityczny 1000 µF/16 V | 1–2 | przy VCC modułu |
| Kondensator ceramiczny 100 nF | 1–2 | przy VCC modułu i AVR |
| Rezystor 10 kΩ | 2 | podciąganie RESET; ew. dzielnik RXD |
| Rezystor 4,7 kΩ | 1 | ew. dzielnik RXD |
| Dioda 1N4007 | 0–6 | zależnie od wariantu zasilania |
| Koszyk 3×AA | 1 | wariant A |
| Płytka stykowa → uniwersalna | 1+1 | prototyp → montaż docelowy |

(ATtiny2313, RPi i przewody już masz.)
