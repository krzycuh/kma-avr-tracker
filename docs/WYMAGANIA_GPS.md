# Wymagania na moduł GPS

## Wymagania obligatoryjne

| # | Wymaganie | Uzasadnienie |
|---|-----------|-------------|
| G1 | **Wyjście UART (TTL, 3.3V)** | Bezpośrednie połączenie z MCU bez konwertera poziomów |
| G2 | **Protokół NMEA 0183** | Standardowy, parser niezależny od producenta |
| G3 | **Zdanie $GPRMC** | Minimum: pozycja (lat/lon), czas UTC, data, prędkość, kurs, status fix |
| G4 | **Baud rate 9600 bps** (domyślny) | Kompatybilność z istniejącym kodem UART, wystarczające dla 1 Hz |
| G5 | **Napięcie zasilania 3.3V** (lub 2.7–5V) | Wspólne zasilanie z MCU i SIM800L, bez dodatkowego regulatora |
| G6 | **Antena** — zintegrowana lub z gniazdem na zewnętrzną | Działanie "z pudełka", opcja lepszej anteny |
| G7 | **Pobór prądu w trybie akwizycji ≤ 50 mA** | Budżet energetyczny: bateria LiPo |
| G8 | **Tryb backup / standby z podtrzymaniem RAM** | Warm/hot start zamiast cold start → szybszy TTFF, mniejsze zużycie energii |

## Wymagania pożądane

| # | Wymaganie | Uzasadnienie |
|---|-----------|-------------|
| G9 | **Pobór w standby/backup ≤ 20 µA** | Minimalizacja zużycia między cyklami GPS |
| G10 | **TTFF (Time To First Fix) ≤ 1s** w hot start | Szybki odczyt po wybudzeniu |
| G11 | **Wsparcie GLONASS / Galileo** | Więcej satelitów = szybszy fix, lepsza dokładność w mieście |
| G12 | **Konfiguracja przez UART** (komendy PMTK / UBX) | Wyłączenie zbędnych zdań NMEA, zmiana update rate |
| G13 | **Mały rozmiar modułu** | Kompaktowość docelowego urządzenia |
| G14 | **Flash na module** (almanac/ephemeris) | Szybszy warm start bez backup battery |
| G15 | **Pin PPS (Pulse Per Second)** | Precyzyjna synchronizacja czasu (opcjonalnie) |
| G16 | **Cena ≤ 25 PLN** | Budżetowość projektu |

## Porównanie modułów

| Cecha | NEO-6M | BN-220 | BN-880 | L80-R | ATGM336H |
|-------|--------|--------|--------|-------|----------|
| **Chipset** | u-blox 6 | u-blox M8030 | u-blox M8030 | Quectel MT3339 | AT6558R |
| **Konstelacje** | GPS | GPS+GLONASS+BeiDou | GPS+GLONASS+BeiDou | GPS+GLONASS | GPS+BeiDou+GLONASS |
| **Napięcie** | 2.7–3.6V | 2.7–3.6V | 3.0–5.0V | 3.0–4.3V | 2.7–3.6V |
| **Prąd akwizycja** | ~45 mA | ~25 mA | ~25 mA | ~25 mA | ~25 mA |
| **Prąd backup** | ~15 µA | ~12 µA | ~12 µA | ~10 µA | ~15 µA |
| **Cold start TTFF** | ~27s | ~26s | ~26s | ~35s | ~32s |
| **Hot start TTFF** | ~1s | ~1s | ~1s | ~1s | ~1s |
| **Dokładność** | 2.5m CEP | 2.5m CEP | 2.5m CEP | 3.0m CEP | 2.5m CEP |
| **Flash na module** | Nie | Tak | Tak | Tak | Nie |
| **Antena** | Zewn. ceramiczna | Zintegrowana | Zintegrowana | Zintegrowana (patch) | Zew. ceramiczna |
| **Rozmiar modułu** | ~25×25mm (z anteną) | 22×20mm | 28×28mm | 16×16mm | ~15×15mm (sam chip) |
| **UART** | Tak, 3.3V | Tak, 3.3V | Tak, 3.3V | Tak, 3.3V | Tak, 3.3V |
| **Komendy** | UBX + NMEA | UBX + NMEA | UBX + NMEA | PMTK | PCAS |
| **Cena (Allegro/AliExpress)** | ~12–18 PLN | ~20–30 PLN | ~30–40 PLN | ~20–30 PLN | ~8–15 PLN |
| **Dostępność** | Bardzo wysoka | Wysoka | Średnia | Średnia | Wysoka |
| **Spełnia G1-G8** | Tak (G8: potrzeba ext. baterii) | **Tak (wszystkie)** | Tak | Tak | Tak (G8: potrzeba ext. baterii) |

## Rekomendacja

### Dla MVP: **NEO-6M**
- Najtańszy (~12–15 PLN z anteną)
- Najlepiej udokumentowany, ogromna społeczność
- Działa "z pudełka" z domyślnymi ustawieniami NMEA
- Wada: tylko GPS (brak GLONASS), brak Flash na module

### Dla docelowego urządzenia: **BN-220**
- Multi-GNSS (GPS+GLONASS+BeiDou) → szybszy fix, lepsza dokładność w mieście
- Flash na module → szybki warm start bez zewnętrznej baterii backup
- Mały form factor (22×20mm) ze zintegrowaną anteną
- Niski pobór prądu (~25 mA akwizycja, ~12 µA backup)
- Wada: droższy (~25 PLN)

### Budżetowa alternatywa: **ATGM336H**
- Bardzo tani (~8–12 PLN)
- Multi-GNSS (GPS+BeiDou+GLONASS)
- Wymaga zewnętrznej anteny ceramicznej (~3 PLN)
- Mniej dokumentacji (chiński chipset)

## Połączenie z MCU

Niezależnie od modułu, połączenie jest identyczne:

```
GPS Module                    ATmega328P
┌──────────┐                  ┌──────────┐
│ VCC ──────┼── 3.3V ─────────┤ VCC      │
│ GND ──────┼── GND ──────────┤ GND      │
│ TX ───────┼─────────────────┤ PD0 (RX) │  ← NMEA data in
│ RX ───────┼─────────────────┤ PD1 (TX) │  ← konfiguracja GPS (opcja)
└──────────┘                  └──────────┘
```

> **Uwaga:** Jeśli MCU na 5V, linia MCU TX → GPS RX wymaga dzielnika napięcia
> (2 rezystory) lub konwertera poziomów. Linia GPS TX → MCU RX jest OK
> (3.3V > próg HIGH dla 5V AVR).
