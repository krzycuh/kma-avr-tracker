# kma-avr-tracker

## attiny-rpi-2way-com.c — opis
- Dwukierunkowa komunikacja UART 9600 bps (8N1, z U2X) na ATtiny.
- Odbiór w przerwaniu z buforem pierścieniowym 16 B; główna pętla odczytuje bufor i odsyła te same dane (echo).
- LED PD2: sygnalizacja pracy („heartbeat”). LED PD3: aktywność RX/TX; na starcie obie migają 3×.
- Proste funkcje wysyłania znaków/łańcuchów przez UART ułatwiają integrację i testy z RPi.

## attiny-rpi-com.c — opis
- UART 9600 bps (8N1, z U2X); transmisja bez przerwań (polling).
- Okresowo wysyła przykładowy tekst przez UART; dioda na PD2 sygnalizuje wysyłanie.
- Prosty szkic do testów linii TX i integracji z RPi/terminalem szeregowym.