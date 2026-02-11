# Wymagania sprzętowe i lista zakupów

## Architektura docelowa

```
┌─────────┐  UART   ┌──────────────┐  SW UART  ┌──────────┐
│   GPS   │ ──────► │  ATmega328P  │ ────────► │  SIM800L │──► HTTP endpoint
│ (NMEA)  │  HW RX  │              │  TX+RX    │  (GPRS)  │
└─────────┘         └──────────────┘           └──────────┘
                      │  │  │  │
                     LED LED MOSFET  ISP
                     fix act GPS_pwr prog
```

**Komponentów w docelowym urządzeniu: 3 moduły + pasywne**

---

## Lista komponentów

### A. Moduły główne

| # | Komponent | Rekomendacja | Cena | Status | Uwagi |
|---|-----------|-------------|------|--------|-------|
| 1 | **MCU** | ATmega328P-PU (DIP-28) lub Arduino Pro Mini 3.3V/8MHz | 8–15 PLN | **DO ZAKUPU** | Pro Mini = gotowa płytka z regulatorem 3.3V |
| 2 | **Moduł GPS** | NEO-6M (MVP) lub BN-220 (docelowy) | 12–25 PLN | **DO ZAKUPU** | Patrz `WYMAGANIA_GPS.md` |
| 3 | **Moduł GSM** | SIM800L | — | **POSIADANY** | 2G GPRS, wystarczy na HTTP POST |
| 4 | **Karta SIM** | Dowolna z pakietem danych | 5–10 PLN/mies. | **DO ZAKUPU** (jeśli nie posiadasz) | Rozmiar: micro SIM. Operatorzy z 2G: Orange, T-Mobile, Plus |

### B. Zasilanie

| # | Komponent | Rekomendacja | Cena | Uwagi |
|---|-----------|-------------|------|-------|
| 5 | **Bateria** | LiPo 3.7V 1000–2000mAh | 15–25 PLN | Napięcie 3.4–4.2V idealne dla SIM800L (3.4–4.4V) i ATmega328P (2.7–5.5V) |
| 6 | **Ładowarka LiPo** | Moduł TP4056 z ochroną | 3–5 PLN | Ładowanie micro-USB, zabezpieczenie przed przeładowaniem/rozładowaniem |
| 7 | **Kondensator** | Elektrolityczny 1000µF/6.3V + ceramiczny 100nF | 1–2 PLN | **Krytyczne** dla SIM800L — peak current 2A powoduje spadki napięcia bez kondensatora |

> **WAŻNE:** SIM800L przy transmisji ciągnie impulsy do 2A. Bez dużego
> kondensatora przy zasilaniu moduł się resetuje. Kondensator 1000µF+ to
> absolutne minimum.

### C. Elementy pasywne i sygnalizacja

| # | Komponent | Wartość | Ilość | Cena | Uwagi |
|---|-----------|---------|-------|------|-------|
| 8 | **LED** | 3mm, zielona + żółta | 2 | 0.50 PLN | Fix GPS + aktywność GSM |
| 9 | **Rezystor LED** | 330Ω | 2 | 0.20 PLN | Ogranicznik prądu LED |
| 10 | **Rezystor pull-up** | 10kΩ | 1 | 0.10 PLN | RESET pin ATmega328P |
| 11 | **Kondensator bypass** | 100nF ceramiczny | 2 | 0.30 PLN | Po jednym przy VCC ATmega i GPS |
| 12 | **MOSFET N-ch** | 2N7000 lub IRLML6344 | 1 | 0.50 PLN | Sterowanie zasilaniem GPS (Faza 4) |
| 13 | **Kwarc** | 8 MHz | 1 | 0.50 PLN | Opcjonalnie — można użyć wewnętrznego oscylatora |
| 14 | **Kondensatory kwarcu** | 22pF ceramiczne | 2 | 0.20 PLN | Wymagane jeśli kwarc zewnętrzny |

### D. Narzędzia i prototypowanie

| # | Komponent | Rekomendacja | Cena | Status |
|---|-----------|-------------|------|--------|
| 15 | **Programator AVR** | USBasp (~10 PLN) lub Arduino Uno/Nano jako ISP (darmowe jeśli posiadasz) | 0–10 PLN | DO SPRAWDZENIA |
| 16 | **Breadboard** | 830 otworów | 8–12 PLN | DO SPRAWDZENIA |
| 17 | **Przewody breadboard** | Zestaw M-M, M-F | 5–8 PLN | DO SPRAWDZENIA |
| 18 | **Adapter USB-UART** | CP2102 lub CH340 (3.3V!) | 5–8 PLN | Przydatny do debugowania UART niezależnie od RPi |
| 19 | **Antena GSM** | Dołączona do SIM800L lub helical | 0–5 PLN | Zwykle dołączona do modułu |
| 20 | **Antena GPS** | Dołączona do modułu GPS (ceramiczna) | 0 PLN | W zestawie z NEO-6M |

---

## Podsumowanie kosztów

### Wariant minimalny (sam chip + najtańsze moduły)

| Pozycja | Koszt |
|---------|-------|
| ATmega328P-PU (DIP-28) | ~8 PLN |
| NEO-6M z anteną | ~15 PLN |
| SIM800L | posiadany |
| LiPo 1000mAh + TP4056 | ~20 PLN |
| Pasywne (LED, rezystory, kondensatory) | ~5 PLN |
| Karta SIM z danymi | ~5 PLN/mies. |
| **Razem (jednorazowo)** | **~53 PLN** |

### Wariant z Arduino Pro Mini (łatwiejsze prototypowanie)

| Pozycja | Koszt |
|---------|-------|
| Arduino Pro Mini 3.3V/8MHz | ~12 PLN |
| BN-220 (lepszy GPS) | ~25 PLN |
| SIM800L | posiadany |
| LiPo 2000mAh + TP4056 | ~30 PLN |
| Pasywne | ~5 PLN |
| USBasp (jeśli brak programatora) | ~10 PLN |
| **Razem (jednorazowo)** | **~82 PLN** |

---

## Moduł SIM — porównanie opcji

Aktualnie posiadasz SIM800L. Poniżej porównanie na wypadek przyszłej migracji:

| Cecha | SIM800L (posiadany) | SIM7000G | SIM7600E |
|-------|-----------|---------|----------|
| **Sieć** | 2G GSM/GPRS | LTE Cat-M1, NB-IoT, 2G fallback | 4G LTE Cat-4 |
| **GPS wbudowany** | Nie | **Tak (GNSS)** | **Tak (GNSS)** |
| **Prąd idle** | ~7 mA | ~3 mA | ~10 mA |
| **Prąd sleep/PSM** | ~60 µA | **~7 µA (PSM)** | ~1 mA |
| **Prąd TX peak** | 2A | ~400 mA | ~2A |
| **Napięcie** | 3.4–4.4V | 3.0–4.3V | 3.4–4.2V |
| **AT komendy** | Standard | Kompatybilne z SIM800 | Kompatybilne z SIM800 |
| **Cena** | ~15 PLN | ~40–60 PLN | ~60–90 PLN |
| **Przyszłościowość** | Niska (2G EOL) | **Wysoka** | Wysoka |
| **Eliminuje osobny GPS?** | Nie | **Tak** | **Tak** |

> **Uwaga o SIM7000G:** Gdybyś zdecydował się na SIM7000G zamiast SIM800L+GPS,
> to **eliminujesz osobny moduł GPS** (SIM7000G ma wbudowany GNSS).
> Urządzenie = ATmega328P + SIM7000G + bateria. Droższe (~50 PLN więcej),
> ale mniej komponentów, niższy pobór prądu (PSM 7 µA), przyszłościowe (LTE).

---

## Schemat zasilania

```
        ┌──────────────────────────────────────────────────┐
        │                     LiPo 3.7V                    │
        │                    (3.4–4.2V)                    │
        └───────────┬──────────────┬───────────────────────┘
                    │              │
              ┌─────┴─────┐  ┌────┴────────────────────┐
              │  ATmega   │  │  SIM800L                 │
              │  328P     │  │  (bezpośrednio z LiPo,   │
              │  VCC      │  │   z kondensatorem 1000µF) │
              └───────────┘  └──────────────────────────┘
                    │
              ┌─────┴─────┐
              │ MOSFET    │──── GPS Module VCC
              │ (sterowany│     (tylko gdy MCU włączy)
              │  przez MCU)│
              └───────────┘
```

**Kluczowe:**
- LiPo 3.7V (zakres 3.4–4.2V) zasilanie bezpośrednie na SIM800L i ATmega328P
- Bez regulatora napięcia — oszczędność komponentu i strat energii
- ATmega328P: działa w zakresie 2.7–5.5V (z zegarem 8 MHz)
- SIM800L: działa w zakresie 3.4–4.4V
- GPS: większość modułów działa od 2.7V (OK z 3.4V min LiPo)
- MOSFET steruje zasilaniem GPS — MCU może wyłączyć GPS między cyklami

---

## Wymagania dot. karty SIM

| Wymaganie | Szczegóły |
|-----------|-----------|
| Rozmiar | **Micro SIM** (SIM800L slot) |
| Sieć | 2G GSM/GPRS (SIM800L obsługuje tylko 2G) |
| Dane | Pakiet danych GPRS (nawet 10 MB/mies. wystarczy) |
| PIN | **Wyłączony** (tracker nie obsługuje wpisywania PIN) |
| Operator | Orange, T-Mobile lub Plus (mają 2G). **Nie Play** (wyłącza 2G). |
| APN | Znany i skonfigurowany w firmware (`internet`, `TM` itp.) |

> **Kalkulacja transferu:** Jeden HTTP GET z pozycją to ~200 B.
> Przy raportowaniu co 5 min = 12/h × 24h = 288 requestów/dzień = ~57 KB/dzień.
> Pakiet 100 MB/mies. wystarczy z ogromnym zapasem.

---

## Gdzie kupić (Polska)

| Sklep | Typ | Uwagi |
|-------|-----|-------|
| [Botland](https://botland.com.pl) | Sklep elektroniczny | Szybka wysyłka, dobry wybór modułów |
| [Kamami](https://kamami.pl) | Sklep elektroniczny | Szeroki asortyment AVR |
| [Allegro](https://allegro.pl) | Marketplace | Najtaniej, ale dłuższa wysyłka (AliExpress sellers) |
| [TME](https://tme.eu) | Dystrybutor | Oryginalne komponenty, hurtowe ceny |
| [AliExpress](https://aliexpress.com) | Import | Najtaniej, wysyłka 2-4 tygodnie |
