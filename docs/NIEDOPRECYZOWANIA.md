# Niedoprecyzowania i pytania otwarte

Dokument zbiera wszystkie nierozstrzygnięte kwestie, które muszą zostać wyjaśnione przed
lub w trakcie implementacji trackera GPS na ATtiny.

---

## 1. Wybór mikrokontrolera

| Pytanie | Kontekst |
|---------|----------|
| **Który dokładnie ATtiny?** | Obecny kod celuje w ATtiny2313 (2 KB Flash, 128 B RAM, 128 B EEPROM, 1 UART sprzętowy). To bardzo ograniczone zasoby dla GPS trackera, który musi parsować NMEA i obsługiwać komunikację. Alternatywy: ATtiny841 (2x UART), ATtiny1614/3216 (nowa seria, więcej pamięci), ATmega328P (jeśli ATtiny nie wystarczy). |
| **Czy limity pamięci ATtiny2313 są akceptowalne?** | Zdanie NMEA GGA ma ~80 znaków. Przy 128 B RAM i 16 B buforze kołowym parsowanie w locie jest konieczne — brak miejsca na buforowanie pełnych zdań. Czy to akceptowalne ograniczenie? |
| **Taktowanie i źródło zegara** | Kod zakłada 8 MHz z wewnętrznym oscylatorem (/8 = 1 MHz efektywne). Czy wystarczająca dokładność dla UART GPS (zwykle 9600 bps)? Czy rozważamy zewnętrzny kwarc? |

---

## 2. Moduł GPS

| Pytanie | Kontekst |
|---------|----------|
| **Który moduł GPS?** | Popularne opcje: NEO-6M, NEO-7M, NEO-M8N (u-blox), BN-220, L80/L86 (Quectel). Różnią się ceną, poborem prądu, czułością, wsparciem GLONASS/Galileo. |
| **Protokół komunikacji** | Większość modułów GPS domyślnie wysyła NMEA przez UART (9600 bps). Czy potrzebujemy UBX (binarny protokół u-blox) dla lepszej wydajności? |
| **Które zdania NMEA parsować?** | `$GPGGA` (pozycja + fix quality), `$GPRMC` (pozycja + prędkość + data), `$GPGLL` (tylko pozycja). Minimalistycznie wystarczy `$GPRMC`. |
| **Tryb pracy GPS** | Ciągły (stale włączony), periodic (budzi się co N sekund), one-shot (na żądanie)? Ma bezpośredni wpływ na pobór prądu i architekturę. |

---

## 3. Transmisja danych lokalizacyjnych

| Pytanie | Kontekst |
|---------|----------|
| **Jak dane mają opuszczać urządzenie?** | Możliwości: (a) UART do Raspberry Pi (jak teraz), (b) moduł GSM/GPRS (SIM800L) — SMS lub HTTP, (c) LoRa (duży zasięg, mały transfer), (d) Bluetooth/BLE, (e) zapis na kartę SD / EEPROM i odczyt offline. |
| **Czy Raspberry Pi jest częścią docelowego rozwiązania?** | Obecna architektura testowa używa RPi jako odbiornika UART. Czy w finalnym produkcie RPi jest hubem (np. agregacja + wysyłka do chmury), czy tracker ma być samodzielny? |
| **Częstotliwość raportowania** | Co ile sekund/minut ma być wysyłana pozycja? To determinuje wymagania energetyczne i przepustowość łącza. |
| **Format danych wyjściowych** | Surowe NMEA? Przetworzony tekst (lat, lon, time)? Binarny pakiet? JSON? Protokół do ustalenia. |

---

## 4. Zasilanie i energooszczędność

| Pytanie | Kontekst |
|---------|----------|
| **Źródło zasilania** | Bateria LiPo? AA/AAA? Zasilanie USB? Solar? Zasilanie samochodowe 12V? |
| **Wymagany czas pracy na baterii** | Godziny, dni, tygodnie? To determinuje strategię sleep mode i duty cycle GPS. |
| **Tryby uśpienia** | ATtiny2313 wspiera Power-down (~0.1 µA) i Idle mode. Moduł GPS ma własne tryby sleep. Jaka strategia zarządzania energią? |
| **Napięcie pracy** | ATtiny2313 działa 2.7–5.5V. Moduły GPS typowo 3.3V. Moduły GSM wymagają 3.4–4.4V z peak current ~2A. Czy potrzebny regulator/konwerter? |

---

## 5. Architektura sprzętu

| Pytanie | Kontekst |
|---------|----------|
| **Ile kanałów UART potrzebujemy?** | ATtiny2313 ma tylko 1 UART sprzętowy. GPS potrzebuje UART. Komunikacja z hostem/modułem GSM też potrzebuje UART. Opcje: software UART (bitbanging), multipleksowanie, wybór MCU z 2 UART. |
| **Dodatkowe peryferia** | Czy potrzebujemy: I2C (czujniki, OLED), SPI (karta SD, LoRa), ADC (pomiar baterii), PWM (buzzer)? |
| **Forma fizyczna** | Płytka prototypowa? PCB? Gotowy moduł? Wymiary? Obudowa? |
| **Antena GPS** | Zintegrowana w module? Zewnętrzna (ceramic patch, helical)? Ma wpływ na form factor i czułość. |

---

## 6. Środowisko budowania i toolchain

| Pytanie | Kontekst |
|---------|----------|
| **System budowania** | CMake został usunięty w ostatnim commicie. Czy wracamy do CMake, używamy PlatformIO, czystego Makefile, czy Arduino framework? |
| **Programator** | Jaki programator AVR? USBasp, AVRISP mkII, Arduino as ISP, JTAG? To wpływa na workflow flashowania. |
| **Platforma deweloperska** | Linux (RPi), Windows, macOS? Wszystkie? |
| **CI/CD** | Czy chcemy automatyczne budowanie i testy w pipeline GitHub? |

---

## 7. Testowanie

| Pytanie | Kontekst |
|---------|----------|
| **Testy jednostkowe kodu AVR** | Czy testujemy logikę parsowania NMEA na hoście (x86) przed flashowaniem na AVR? Frameworki: Unity, CMocka, natywna kompilacja z mockami. |
| **Symulacja** | Czy używamy symulatora AVR (simavr, Proteus) do testów przed hardware? |
| **Testowanie integracyjne** | Obecne skrypty Python testują UART. Czy rozszerzamy je o automatyczne testy GPS (replay NMEA)? |
| **Testy hardware-in-the-loop** | Czy budujemy stanowisko testowe z RPi + ATtiny + symulowanym GPS (NMEA replay przez UART)? |

---

## 8. Zakres funkcjonalny MVP

| Pytanie | Kontekst |
|---------|----------|
| **Co jest MVP (Minimum Viable Product)?** | Propozycja: ATtiny odbiera NMEA z GPS, parsuje pozycję, wysyła przez UART do RPi. Bez GSM, bez sleep, bez baterii. |
| **Geofencing / alarmy** | Czy tracker ma reagować na wejście/wyjście ze strefy? |
| **Logowanie trasy** | Czy zapisujemy historię pozycji (EEPROM, zewnętrzna pamięć Flash)? |
| **Interfejs użytkownika** | LED status? Buzzer? Przycisk? Wyświetlacz? Aplikacja na telefon? Dashboard webowy? |

---

## 9. Aspekty prawne i regulacyjne

| Pytanie | Kontekst |
|---------|----------|
| **Certyfikacja** | Jeśli urządzenie ma być sprzedawane: CE, FCC. Jeśli hobbystycznie — nie dotyczy. |
| **GPS i prywatność** | Czy śledzenie osób wymaga zgody? Regulacje RODO. |
| **Częstotliwości radiowe** | Jeśli używamy LoRa/GSM — regulacje pasma ISM, karta SIM. |

---

## Priorytetyzacja pytań

**Krytyczne (blokują rozpoczęcie pracy):**
1. Wybór MCU (ATtiny2313 vs alternatywa)
2. Moduł GPS
3. Metoda transmisji danych (UART do RPi vs GSM vs LoRa)
4. System budowania

**Ważne (wpływają na architekturę):**
5. Zasilanie
6. Liczba kanałów UART / software UART
7. Zakres MVP

**Mogą poczekać:**
8. Form factor / PCB
9. CI/CD
10. Certyfikacja
