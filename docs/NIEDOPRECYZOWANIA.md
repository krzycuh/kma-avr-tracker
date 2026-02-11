# Niedoprecyzowania i pytania otwarte

Dokument zbiera nierozstrzygnięte kwestie, które muszą zostać wyjaśnione przed
lub w trakcie implementacji trackera GPS na AVR.

**Rozstrzygnięte decyzje (z rozmowy):**
- RPi NIE jest częścią docelowego urządzenia — tylko do testów
- Transmisja danych: karta SIM → HTTP endpoint na serwer
- Posiadany moduł: SIM800L (2G)
- Priorytet: niski koszt, minimalna liczba komponentów, niskie zużycie energii

---

## 1. Wybór MCU

| Pytanie | Kontekst |
|---------|----------|
| **ATmega328P czy inny?** | ATtiny2313 jest za mały (2 KB Flash, 128 B RAM, 1 UART). Rekomendacja: ATmega328P (32 KB Flash, 2 KB RAM, 1 HW UART + SW UART). Alternatywy: ATtiny841 (2 UART, 8 KB Flash — ciasno), ATtiny1614 (16 KB, UPDI). Czy akceptujesz ATmega328P? |
| **Gotowy moduł Arduino Nano/Pro Mini czy sam chip?** | Arduino Pro Mini 3.3V (~8 PLN) to ATmega328P na płytce z regulatorem i złączem ISP — oszczędza lutowanie. Sam chip DIP-28 (~8 PLN) + kwarc + kondensatory to bardziej "bare metal" ale wymaga więcej pracy. Co preferujesz? |
| **Taktowanie** | 8 MHz (wewnętrzny oscylator, wystarczy dla UART 9600) vs 16 MHz (zewnętrzny kwarc, wymagany dla UART 115200 z SIM800L jeśli potrzebny). |

---

## 2. Moduł GPS

| Pytanie | Kontekst |
|---------|----------|
| **Który moduł?** | Wymagania spisane w `WYMAGANIA_GPS.md`. Potrzebna decyzja: NEO-6M (~15 PLN, popularny), BN-220 (~25 PLN, mały, z Flash), BN-880 (~35 PLN, GPS+kompas). |
| **Które zdania NMEA parsować?** | `$GPRMC` daje: pozycję, czas, datę, prędkość, kurs. Czy to wystarcza? Czy potrzebne `$GPGGA` (liczba satelitów, HDOP, wysokość)? |

---

## 3. Serwer i endpoint HTTP

| Pytanie | Kontekst |
|---------|----------|
| **Jaki endpoint?** | Jaka domena/URL? Format danych: GET z parametrami (`?lat=51.1&lon=17.0&t=123456`) czy POST z body (JSON, form-data)? |
| **Autentykacja** | Czy endpoint wymaga API key, tokenu, basic auth? Czy wystarczy "security through obscurity" (trudny do zgadnięcia URL)? |
| **Odpowiedź serwera** | Czy tracker ma czytać odpowiedź HTTP? (np. nowy interwał, komenda "wyłącz się"). Czy fire-and-forget? |
| **Czy serwer/backend jest w zakresie projektu?** | Czy piszemy też backend (np. prosty serwer Flask/Express z bazą danych i mapą), czy zakładamy istniejący endpoint? |

---

## 4. Zasilanie

| Pytanie | Kontekst |
|---------|----------|
| **Źródło zasilania** | LiPo 3.7V (idealne dla SIM800L: 3.4–4.4V) vs 3xAA/AAA (4.5V, potrzebny regulator) vs zasilanie USB/samochodowe (5V/12V). |
| **Wymagany czas pracy** | Godziny? Dni? Tygodnie? Przy LiPo 1000mAh i cyklu co 5 min: ~2-3 dni. Przy co 30 min: ~1-2 tygodnie. |
| **Czy urządzenie ma działać non-stop czy na żądanie?** | Włącz → śledzi → wyłącz ręcznie? Czy zawsze aktywne z duty cycle? |

---

## 5. Interwał raportowania

| Pytanie | Kontekst |
|---------|----------|
| **Co ile wysyłać pozycję?** | Co 30s? 1 min? 5 min? 30 min? Bezpośrednio wpływa na żywotność baterii i koszty transmisji danych. |
| **Stały interwał czy adaptatywny?** | Np. co 30s gdy się porusza, co 5 min gdy stoi? Wymaga detekcji ruchu (porównanie pozycji lub akcelerometr). |

---

## 6. Zachowanie przy braku zasięgu

| Pytanie | Kontekst |
|---------|----------|
| **Buforowanie offline?** | Czy przy braku zasięgu GSM zapisywać pozycje w EEPROM/RAM i wysłać po powrocie zasięgu? ATmega328P ma 1 KB EEPROM (~50 pozycji). |
| **Retry policy** | Ile razy ponawiać HTTP request przy błędzie? Timeout? |

---

## 7. Obudowa i montaż

| Pytanie | Kontekst |
|---------|----------|
| **Gdzie będzie montowany tracker?** | Samochód, rower, plecak, zwierzę? Wpływa na: rozmiar, wodoodporność, mocowanie, antena. |
| **Wodoodporność** | IP rating? Gotowa obudowa? Druk 3D? |
| **Rozmiar** | Ograniczenia wymiarowe? "Jak najmniejsze" czy "nie ma znaczenia"? |

---

## 8. Programator i narzędzia

| Pytanie | Kontekst |
|---------|----------|
| **Jaki programator AVR posiadasz?** | USBasp (~10 PLN), AVRISP mkII, Arduino Uno/Nano jako ISP (bezpłatne jeśli posiadasz). Wpływa na skrypty flashowania. |
| **Analizator logiczny / oscyloskop?** | Przydatne do debugowania UART. Masz? Jeśli nie — tanie klony Saleae (~30 PLN). |

---

## Priorytetyzacja

**Krytyczne (blokują implementację):**
1. Wybór MCU (ATmega328P — do potwierdzenia)
2. Moduł GPS (do zakupu)
3. Endpoint HTTP (URL, format, auth)

**Ważne (wpływają na architekturę):**
4. Źródło zasilania
5. Interwał raportowania
6. Programator AVR

**Mogą poczekać:**
7. Buforowanie offline
8. Obudowa / form factor
9. Backend / dashboard
