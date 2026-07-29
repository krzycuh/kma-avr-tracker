#!/usr/bin/env python3
"""
Symulator modułu SIM800L - do testowania firmware'u trackera (avr-app/gps-tracker.c)
na stanowisku RPi <-> ATtiny2313, zanim podłączysz prawdziwy moduł.

Uruchomienie na RPi (AVR podłączony do UART tak jak przy uart-request-test.py):

    python3 sim800l-simulator.py                      # domyślnie /dev/ttyS0, 9600
    python3 sim800l-simulator.py --port /dev/ttyS0

Symulator odpowiada na komendy AT tak jak SIM800L (OK, +CPIN: READY, +CREG: 0,1,
+SAPBR, +CIPGSMLOC oraz klient HTTP: HTTPINIT/HTTPPARA/HTTPACTION/HTTPTERM).
Gdy firmware wyśle raport (AT+HTTPPARA="URL",...), symulator wypisuje wyraźnie
odebrany URL wraz z rozbitymi parametrami (token, lat, lon) - to jest moment,
w którym prawdziwy moduł strzeliłby HTTP GET na Twój serwer.

Komendy interaktywne (wpisz w terminalu symulatora + Enter):
    noreg    - symuluj brak zasięgu (+CREG: 0,2)
    reg      - przywróć zasięg (+CREG: 0,1)
    httpfail - przełącz symulację błędu HTTP (+HTTPACTION: 0,601 zamiast 0,200)
    quit     - zakończ
"""

import argparse
import threading
import time
from urllib.parse import urlparse, parse_qs

import serial

# Pozycja "stacji bazowej" zwracana przez symulator (centrum Warszawy)
FAKE_LONGITUDE = "21.017532"
FAKE_LATITUDE = "52.237049"


class Sim800lSimulator:
    def __init__(self, port, baudrate):
        self.ser = serial.Serial(port=port, baudrate=baudrate, timeout=0.1)
        self.echo = True          # SIM800L po starcie ma włączone echo komend
        self.registered = True    # czy symulujemy zasięg sieci 2G
        self.http_ok = True       # czy HTTPACTION ma zwracać sukces (200) czy błąd (601)
        self.lock = threading.Lock()

    # ------------------------------------------------------------------ wysyłanie
    def send_line(self, text):
        """Wyślij linię zakończoną CRLF (tak formatuje odpowiedzi SIM800L)."""
        with self.lock:
            self.ser.write(f"\r\n{text}\r\n".encode())
        print(f"[sim ->] {text}")

    def send_raw(self, data):
        with self.lock:
            self.ser.write(data)

    # ------------------------------------------------------------------ raport HTTP
    def show_report(self, url):
        """Wypisz raport pozycji, który firmware chce wysłać na serwer."""
        parsed = urlparse(url)
        params = parse_qs(parsed.query)
        token = params.get("token", ["<brak>"])[0]
        lat = params.get("lat", ["<brak>"])[0]
        lon = params.get("lon", ["<brak>"])[0]
        print("=" * 60)
        print("[sim] RAPORT HTTP GET (tak wysłałby go prawdziwy moduł):")
        print(f"[sim]   URL:   {url}")
        print(f"[sim]   token: {token}")
        print(f"[sim]   lat:   {lat}   lon: {lon}")
        print(f"[sim]   mapa:  https://maps.google.com/maps?q={lat},{lon}")
        print("=" * 60)

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
        elif upper.startswith("AT+CREG?"):
            status = "+CREG: 0,1" if self.registered else "+CREG: 0,2"
            self.send_line(status)
            self.send_line("OK")
        elif upper.startswith("AT+SAPBR=2,1"):
            self.send_line('+SAPBR: 1,1,"10.89.193.1"')
            self.send_line("OK")
        elif upper.startswith("AT+CIPGSMLOC"):
            # format: +CIPGSMLOC: <kod>,<długość>,<szerokość>,<data>,<czas>
            now = time.strftime("%Y/%m/%d,%H:%M:%S", time.gmtime())
            self.send_line(f"+CIPGSMLOC: 0,{FAKE_LONGITUDE},{FAKE_LATITUDE},{now}")
            self.send_line("OK")
        elif upper.startswith('AT+HTTPPARA="URL"'):
            # wyciągnij URL z: AT+HTTPPARA="URL","http://..."
            try:
                url = cmd.split(",", 1)[1].strip().strip('"')
            except IndexError:
                url = "<nie udało się sparsować>"
            self.show_report(url)
            self.send_line("OK")
        elif upper.startswith("AT+HTTPACTION"):
            self.send_line("OK")
            # prawdziwy moduł wysyła URC po wykonaniu żądania - symulujemy opóźnienie sieci
            time.sleep(1.5)
            if self.http_ok:
                self.send_line("+HTTPACTION: 0,200,2")
            else:
                self.send_line("+HTTPACTION: 0,601,0")   # 601 = network error
        else:
            # pozostałe komendy (HTTPINIT, HTTPTERM, CMGF, SAPBR=3/1/0, CSCLK, CFUN,
            # CREG=0, IPR, AT&W, ...) kwitujemy OK - tak jak prawdziwy moduł
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
        print("[sim] Komendy: noreg | reg | httpfail | quit")
        while True:
            try:
                cmd = input().strip().lower()
            except EOFError:
                return
            if cmd == "noreg":
                self.registered = False
                print("[sim] Symuluję brak zasięgu (+CREG: 0,2)")
            elif cmd == "reg":
                self.registered = True
                print("[sim] Zasięg przywrócony (+CREG: 0,1)")
            elif cmd == "httpfail":
                self.http_ok = not self.http_ok
                stan = "BŁĄD 601" if not self.http_ok else "sukces 200"
                print(f"[sim] HTTPACTION będzie teraz zwracać: {stan}")
            elif cmd == "quit":
                return
            elif cmd:
                print("[sim] Nieznana komenda. Dostępne: noreg | reg | httpfail | quit")


def main():
    parser = argparse.ArgumentParser(description="Symulator SIM800L dla testów trackera")
    parser.add_argument("--port", default="/dev/ttyS0", help="port szeregowy (domyślnie /dev/ttyS0)")
    parser.add_argument("--baud", type=int, default=9600, help="prędkość (domyślnie 9600)")
    args = parser.parse_args()

    sim = Sim800lSimulator(args.port, args.baud)
    print(f"[sim] Symulator SIM800L na {args.port} @ {args.baud} bps")

    reader = threading.Thread(target=sim.serial_loop, daemon=True)
    reader.start()
    try:
        sim.console_loop()
    except KeyboardInterrupt:
        pass
    print("[sim] Koniec")


if __name__ == "__main__":
    main()
