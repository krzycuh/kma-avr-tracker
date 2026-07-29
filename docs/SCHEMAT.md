# Projekt układu: ATmega328P + SIM7000E HAT (+ RPi do programowania i testów)

Moduł radiowy siedzi na gotowej płytce (Waveshare SIM7000E NB-IoT HAT), która załatwia
zasilanie modułu, gniazdo SIM, anteny i poziomy logiczne — my projektujemy tylko część
AVR i połączenia. Konwerter poziomów HAT-a zostawiamy w domyślnym trybie **3,3 V**
i całą logikę (AVR, RPi) prowadzimy w 3,3 V.

## 1. Wykorzystane piny ATmega328P (DIP-28)

```
                     ATmega328P
                   ┌─────∪─────┐
 RESET (RPi/10kΩ)──┤1  PC6  PC5 28├
   RXD ← HAT TXD ──┤2  PD0  PC4 27├
   TXD → HAT RXD ──┤3  PD1  PC3 26├
        (LED info)─┤4  PD2  PC2 25├
   LED statusu ────┤5  PD3  PC1 24├
   PWR HAT-a ──────┤6  PD4  PC0 23├
   VCC 3,3V ───────┤7  VCC  GND 22├── GND
   GND ────────────┤8  GND AREF 21├
                   ┤9  PB6 AVCC 20├── VCC 3,3V (AVCC MUSI być podłączone!)
                   ┤10 PB7  PB5 19├── SCK  (RPi GPIO11 - programowanie)
                   ┤11 PD5  PB4 18├── MISO (RPi GPIO9 - programowanie)
                   ┤12 PD6  PB3 17├── MOSI (RPi GPIO10 - programowanie)
                   ┤13 PD7  PB2 16├
                   ┤14 PB0  PB1 15├
                   └───────────────┘
```

| Pin AVR | Sygnał | Programowanie (RPi, BCM) | Runtime — testy (RPi) | Runtime — tracker (HAT) |
|---|---|---|---|---|
| 1 (PC6) | RESET | GPIO4 | — | podciągnięcie 10 kΩ do VCC |
| 2 (PD0) | UART RXD | — | GPIO14 (TXD) | TXD HAT-a |
| 3 (PD1) | UART TXD | — | GPIO15 (RXD) | RXD HAT-a |
| 5 (PD3) | dioda LED | — | dioda LED | dioda statusu (3× po raporcie) |
| 6 (PD4) | sterowanie PWR | — | — | pin PWR HAT-a (impuls włączający moduł) |
| 17 (PB3) | MOSI | GPIO10 | — | — |
| 18 (PB4) | MISO | GPIO9 | — | — |
| 19 (PB5) | SCK | GPIO11 | — | — |
| 7+20 | VCC+AVCC | 3,3 V z RPi | 3,3 V z RPi | 3,3 V (stabilizator) |
| 8+22 | GND | wspólna z RPi | wspólna z RPi | wspólna z HAT-em |

**Uwagi:**
- **AVCC (pin 20) musi być połączone z VCC** — bez tego port C i ADC nie działają,
  a ATmega potrafi zachowywać się nieprzewidywalnie. AREF zostaw wolny.
- Kondensatory 100 nF: między pinami 7–8 oraz 20–22, jak najbliżej układu.
- Zegar: wewnętrzny RC 8 MHz z dzielnikiem /8 = 1 MHz — **fabryczne fuse bity**
  (`lfuse=0x62`, `hfuse=0xd9`), kwarc niepotrzebny, niczego nie przepalamy.
- `avr-build`/avrdude: `-mmcu=atmega328p` / `-p m328p`, piny linuxgpio bez zmian
  (reset=4, sck=11, mosi=10, miso=9).

## 2. Połączenie ATmega328P ↔ SIM7000E HAT (wersja docelowa)

```
 zasilacz/powerbank 5V (min. 2A!) ─────────────────┐
                                                   │
        stabilizator 3,3V (np. AMS1117) ◄──────────┤
             │                                     │
   3,3V ─────┤                                     │
             │                                ┌────┴────────────────┐
        ┌────┴────┐                           │  SIM7000E HAT       │
        │ ATmega  │                           │                     │
 10kΩ ──┤ RESET   │                           │  ANT LTE ═╗         │
(do 3,3V)         │                           │  ANT GPS ═╗         │
        │ PD0 (2) ├────────────◄──────────────┤ TXD                 │
        │ PD1 (3) ├────────────►──────────────┤ RXD                 │
        │ PD4 (6) ├────────────►──────────────┤ PWR                 │
        │ PD3 (5) ├──[LED]──[470Ω]──GND       │ [SIM] [konw. 3,3V]  │
        │ GND(8,22)├──────────────────────────┤ GND                 │
        └─────────┘                           │ 5V                  │
                                              └────┬────────────────┘
                                                   │
                                     5V ───────────┘
```

- **TXD/RXD na krzyż**: opisy pinów HAT-a są z perspektywy modułu (TXD = wyjście
  modułu → do PD0/RXD AVR). Jeśli po podłączeniu brak odpowiedzi na AT — pierwsza
  rzecz do sprawdzenia: zamień te dwa przewody.
- **PWR**: firmware daje na PD4 impuls wysoki ~2 s, gdy moduł nie odpowiada na AT —
  to odpowiednik przytrzymania przycisku zasilania HAT-a. Na płytce Waveshare ten
  sygnał jest na złączu sterującym; jeśli Twój egzemplarz go nie wyprowadza na
  goldpinach, to jest to pozycja GPIO4 gniazda 40-pin HAT-a.
- **Zasilanie**: HAT bierze 5 V (przetwornica i kondensatory ma na pokładzie — sekcja
  zasilania z czasów SIM800L jest zbędna). AVR bierze 3,3 V ze stabilizatora
  (AMS1117-3.3 + 2 kondensatory) albo — na etapie testów — z pinu 3,3 V RPi.
- Konwerter poziomów HAT-a: zostaw domyślne **3,3 V** (przelutowanie rezystora 0 Ω
  na 5 V jest możliwe, ale nam niepotrzebne).
- Karta SIM z **wyłączonym PIN-em** (albo PIN 1111 — patrz `SIM_PIN` w `gps-tracker.c`);
  antena GPS musi „widzieć niebo" (okno/deska rozdzielcza).

## 3. Stanowisko testowe M1–M2 (RPi jako symulator SIM7000E)

To samo okablowanie, którego używałeś przy ATtiny — zmienia się tylko numeracja
fizycznych pinów układu:

| RPi (BCM) | ATmega328P | Rola |
|---|---|---|
| GPIO4 | pin 1 (RESET) | programowanie (linuxgpio) |
| GPIO10 (MOSI) | pin 17 (PB3) | programowanie |
| GPIO9 (MISO) | pin 18 (PB4) | programowanie |
| GPIO11 (SCK) | pin 19 (PB5) | programowanie |
| GPIO14 (TXD) | pin 2 (PD0/RXD) | RPi „udaje" TXD modułu |
| GPIO15 (RXD) | pin 3 (PD1/TXD) | odbiór komend AT z AVR |
| 3,3 V | piny 7 i 20 (VCC+AVCC) | zasilanie AVR |
| GND | piny 8 i 22 | wspólna masa |

Uruchomienie: `python3 uart-test/sim7000-simulator.py`. Pamiętaj o zwolnieniu portu
`/dev/ttyS0` (konsola szeregowa RPi wyłączona). Programowanie i symulator działają
naprzemiennie bez przepinania.

## 4. Nauka HAT-a bez AVR (M4) i podsłuch (debug w M5)

- **M4:** HAT łączysz po USB (kabel w zestawie) z RPi/laptopem — zgłosi się jako
  kilka portów szeregowych; komendy AT wydajesz przez `minicom` na porcie AT modemu.
  Zasilanie HAT-a nadal z 5 V (sam USB może nie udźwignąć szczytów transmisji).
- **Debug M5:** podepnij **tylko RXD RPi (GPIO15)** do linii TXD AVR albo TXD HAT-a —
  czytasz całą konwersację (`minicom -D /dev/ttyS0 -b 9600`). Wspólna masa wymagana;
  TXD RPi zostaw odłączone.

## 5. Lista zakupów (BOM) — prototyp

| Element | Ilość | Uwagi |
|---|---|---|
| Waveshare SIM7000E NB-IoT HAT | 1 | ✅ na liście (anteny i kabel USB w zestawie) |
| ATmega328P (DIP-28) | 1 | ✅ na liście |
| Karta SIM z pakietem danych | 1 | zwykły prepaid Orange OK; PIN wyłączony |
| Zasilacz/ładowarka 5 V min. 2 A | 1 | do pracy HAT-a standalone |
| Stabilizator 3,3 V (AMS1117 moduł) | 1 | zasilanie AVR poza RPi (M6); wcześniej 3,3 V z RPi |
| Kondensatory 100 nF | 4 | odsprzęganie VCC/AVCC |
| Rezystor 10 kΩ | 1–2 | pull-up RESET |
| Rezystor 470 Ω + LED | 1–2 | dioda statusu |
| Przewody dupont M-F | ~10 | goldpiny HAT-a ↔ płytka stykowa |
| Podstawka DIP-28, płytka uniwersalna | 1+1 | dopiero na montaż docelowy (M7) |
