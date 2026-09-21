#!/usr/bin/env python3
"""
Symulator modułu SIM7000E - do testowania firmware'u trackera (avr-app/gps-tracker.c)
na stanowisku RPi <-> ATmega328P, zanim podłączysz prawdziwy HAT.

Uruchomienie na RPi (AVR podłączony do UART tak jak przy uart-request-test.py):

    python3 sim7000-simulator.py                      # domyślnie /dev/ttyS0, 9600
    python3 sim7000-simulator.py --port /dev/ttyS0

Symulator odpowiada na komendy AT tak jak SIM7000E:
 - konfiguracja: ATE0, AT+IPR, AT&W, AT+CPIN?, AT+CNMP, AT+CMNB
 - sieć: AT+CGATT? (attach), AT+CNACT (połączenie danych, URC +APP PDP: ACTIVE)
 - GNSS: AT+CGNSPWR, AT+CGNSINF (najpierw kilka odpowiedzi bez fixu, potem fix
   z pozycją centrum Warszawy - jak prawdziwy odbiornik łapiący satelity)
 - HTTP (aplikacja SH): AT+SHCONF/SHCONN/SHSTATE?/SHREQ/SHDISC
   Gdy firmware wyśle żądanie (AT+SHREQ), symulator wypisuje wyraźnie pełny URL
   raportu z rozbitymi parametrami (token, lat, lon).

Komendy interaktywne (wpisz w terminalu symulatora + Enter):
    nofix    - GNSS przestaje mieć fix (np. wjazd do garażu)
    fix      - przywróć fix GNSS
    noreg    - symuluj brak sieci (+CGATT: 0)
    reg      - przywróć sieć (+CGATT: 1)
    httpfail - przełącz symulację błędu HTTP (+SHREQ: "GET",404 zamiast 200)
    quit     - zakończ
"""

import argparse
import threading
import time
from urllib.parse import parse_qs, urlparse

import serial

# Pozycja "z satelitów" zwracana przez symulator (centrum Warszawy)
FAKE_LATITUDE = "52.237049"
FAKE_LONGITUDE = "21.017532"

# Ile pierwszych zapytań CGNSINF ma być bez fixu (symulacja łapania satelitów)
FIX_AFTER_QUERIES = 3


class Sim7000Simulator:
    def __init__(self, port, baudrate):
        self.ser = serial.Serial(port=port, baudrate=baudrate, timeout=0.1)
        self.echo = True            # moduł po starcie ma włączone echo komend
        self.registered = True      # czy symulujemy attach do sieci (+CGATT: 1)
        self.gnss_has_fix = True    # czy GNSS ma "widoczność nieba"
        self.gnss_queries = 0       # licznik zapytań CGNSINF (do symulacji zimnego startu)
        self.http_ok = True         # czy SHREQ ma zwracać 200 czy 404
        self.base_url = ""          # z AT+SHCONF="URL",...
        self.lock = threading.Lock()

    # ------------------------------------------------------------------ wysyłanie
    def send_line(self, text):
        """Wyślij linię zakończoną CRLF (tak formatuje odpowiedzi SIM7000E)."""
        with self.lock:
            self.ser.write(f"\r\n{text}\r\n".encode())
        print(f"[sim ->] {text}")

    def send_raw(self, data):
        with self.lock:
            self.ser.write(data)

    # ------------------------------------------------------------------ raport HTTP
    def show_report(self, path):
        """Wypisz raport pozycji, który firmware chce wysłać na serwer."""
        full_url = self.base_url + path
        params = parse_qs(urlparse(full_url).query)
        token = params.get("token", ["<brak>"])[0]
        lat = params.get("lat", ["<brak>"])[0]
        lon = params.get("lon", ["<brak>"])[0]
        print("=" * 60)
        print("[sim] RAPORT HTTP GET (tak wysłałby go prawdziwy moduł):")
        print(f"[sim]   URL:   {full_url}")
        print(f"[sim]   token: {token}")
        print(f"[sim]   lat:   {lat}   lon: {lon}")
        print(f"[sim]   mapa:  https://maps.google.com/maps?q={lat},{lon}")
        print("=" * 60)

    # ------------------------------------------------------------------ GNSS
    def gnsinf_line(self):
        """Zbuduj linię +CGNSINF jak prawdziwy moduł (run,fix,utc,lat,lon,alt,...)."""
        self.gnss_queries += 1
        utc = time.strftime("%Y%m%d%H%M%S.000", time.gmtime())
        if self.gnss_has_fix and self.gnss_queries > FIX_AFTER_QUERIES:
            return (f"+CGNSINF: 1,1,{utc},{FAKE_LATITUDE},{FAKE_LONGITUDE},"
                    f"110.000,0.00,0.0,1,,1.2,1.5,0.9,,11,7,,,42,,")
        # brak fixu: run=1, fix=0, puste pola pozycji
        return f"+CGNSINF: 1,0,{utc},,,,,,0,,,,,,0,0,,,,,"

    # ------------------------------------------------------------------ komendy AT
    def handle_command(self, line):
        cmd = line.strip()
        if not cmd:
            return
        print(f"[avr ->] {cmd}")

        if self.echo:
            self.send_raw(cmd.encode() + b"\r\n")

        upper = cmd.upper()

        if upper == "AT":
            self.send_line("OK")
        elif upper == "ATE0":
            self.echo = False
            self.send_line("OK")
        elif upper.startswith("AT+CPIN?"):
            self.send_line("+CPIN: READY")
            self.send_line("OK")
        elif upper.startswith("AT+CPIN="):
            self.send_line("OK")
        elif upper.startswith("AT+CGATT?"):
            status = "+CGATT: 1" if self.registered else "+CGATT: 0"
            self.send_line(status)
            self.send_line("OK")
        elif upper.startswith("AT+CGNSINF"):
            self.send_line(self.gnsinf_line())
            self.send_line("OK")
        elif upper.startswith("AT+CNACT=1"):
            self.send_line("OK")
            time.sleep(0.5)
            self.send_line("+APP PDP: ACTIVE")
        elif upper.startswith("AT+CNACT=0"):
            self.send_line("OK")
            time.sleep(0.3)
            self.send_line("+APP PDP: DEACTIVE")
        elif upper.startswith('AT+SHCONF="URL"'):
            # zapamiętaj bazowy URL z: AT+SHCONF="URL","http://host:port"
            try:
                self.base_url = cmd.split(",", 1)[1].strip().strip('"')
            except IndexError:
                self.base_url = "<nie udało się sparsować>"
            print(f"[sim] Bazowy URL serwera: {self.base_url}")
            self.send_line("OK")
        elif upper.startswith("AT+SHSTATE?"):
            self.send_line("+SHSTATE: 1")   # połączony z serwerem
            self.send_line("OK")
        elif upper.startswith("AT+SHREQ="):
            # AT+SHREQ="/sciezka?query",1  -> URC +SHREQ: "GET",<kod>,<długość>
            try:
                path = cmd.split("=", 1)[1].rsplit(",", 1)[0].strip().strip('"')
            except IndexError:
                path = "<nie udało się sparsować>"
            self.show_report(path)
            self.send_line("OK")
            time.sleep(1.5)   # symulacja czasu żądania w sieci
            if self.http_ok:
                self.send_line('+SHREQ: "GET",200,2')
            else:
                self.send_line('+SHREQ: "GET",404,0')
        else:
            # pozostałe komendy (IPR, AT&W, CNMP, CMNB, CGNSPWR, CFUN, SHCONF BODYLEN/
            # HEADERLEN, SHCONN, SHDISC, ...) kwitujemy OK - tak jak prawdziwy moduł
            self.send_line("OK")

    # ------------------------------------------------------------------ pętle
    def serial_loop(self):
        """Czytaj bajty z UART i składaj linie komend AT."""
        line_buf = bytearray()
        while True:
            data = self.ser.read(64)
            for byte in data:
                if byte in (0x0A, 0x0D):
                    if line_buf:
                        self.handle_command(line_buf.decode(errors="replace"))
                        line_buf = bytearray()
                else:
                    line_buf.append(byte)

    def console_loop(self):
        """Komendy interaktywne z klawiatury."""
        print("[sim] Komendy: nofix | fix | noreg | reg | httpfail | quit")
        while True:
            try:
                cmd = input().strip().lower()
            except EOFError:
                return
            if cmd == "nofix":
                self.gnss_has_fix = False
                print("[sim] GNSS bez fixu (garaż/piwnica)")
            elif cmd == "fix":
                self.gnss_has_fix = True
                self.gnss_queries = FIX_AFTER_QUERIES  # fix od następnego zapytania
                print("[sim] GNSS ma fix")
            elif cmd == "noreg":
                self.registered = False
                print("[sim] Symuluję brak sieci (+CGATT: 0)")
            elif cmd == "reg":
                self.registered = True
                print("[sim] Sieć przywrócona (+CGATT: 1)")
            elif cmd == "httpfail":
                self.http_ok = not self.http_ok
                stan = "BŁĄD 404" if not self.http_ok else "sukces 200"
                print(f"[sim] SHREQ będzie teraz zwracać: {stan}")
            elif cmd == "quit":
                return
            elif cmd:
                print("[sim] Nieznana komenda. Dostępne: nofix | fix | noreg | reg | httpfail | quit")


def main():
    parser = argparse.ArgumentParser(description="Symulator SIM7000E dla testów trackera")
    parser.add_argument("--port", default="/dev/ttyS0", help="port szeregowy (domyślnie /dev/ttyS0)")
    parser.add_argument("--baud", type=int, default=9600, help="prędkość (domyślnie 9600)")
    args = parser.parse_args()

    sim = Sim7000Simulator(args.port, args.baud)
    print(f"[sim] Symulator SIM7000E na {args.port} @ {args.baud} bps")

    reader = threading.Thread(target=sim.serial_loop, daemon=True)
    reader.start()
    try:
        sim.console_loop()
    except KeyboardInterrupt:
        pass
    print("[sim] Koniec")


if __name__ == "__main__":
    main()
