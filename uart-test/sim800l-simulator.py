#!/usr/bin/env python3
"""
Symulator modułu SIM800L - do testowania firmware'u trackera (avr-app/gps-tracker.c)
na stanowisku RPi <-> ATtiny2313, zanim podłączysz prawdziwy moduł.

Uruchomienie na RPi (AVR podłączony do UART tak jak przy uart-request-test.py):

    python3 sim800l-simulator.py                      # domyślnie /dev/ttyS0, 9600
    python3 sim800l-simulator.py --port /dev/ttyS0
    python3 sim800l-simulator.py --ri-pin 17          # pin BCM podłączony do PD2 (RI/RING)

Symulator odpowiada na komendy AT tak jak SIM800L (OK, +CPIN: READY, +CREG: 0,1,
+SAPBR, +CIPGSMLOC, tryb wysyłania SMS z znakiem zachęty '>').

Komendy interaktywne (wpisz w terminalu symulatora + Enter):
    ring   - symuluje przychodzące połączenie: wysyła RING + +CLIP,
             a jeśli podano --ri-pin, ściąga linię RI do stanu niskiego na 2s
             (bez --ri-pin trzeba zewrzeć PD2 do GND ręcznie, bo firmware
             czeka na stan niski na pinie zanim zacznie czytać UART!)
    noreg  - przełącz symulację braku zasięgu (+CREG: 0,2)
    reg    - przywróć zasięg (+CREG: 0,1)
    quit   - zakończ

Odebrany SMS (treść między '>' a Ctrl+Z) jest wypisywany na konsoli.
"""

import argparse
import sys
import threading
import time

import serial

# Pozycja "stacji bazowej" zwracana przez symulator (centrum Warszawy)
FAKE_LONGITUDE = "21.017532"
FAKE_LATITUDE = "52.237049"
FAKE_PHONE = "+48123456789"


class Sim800lSimulator:
    def __init__(self, port, baudrate, ri_pin):
        self.ser = serial.Serial(port=port, baudrate=baudrate, timeout=0.1)
        self.echo = True          # SIM800L po starcie ma włączone echo komend
        self.registered = True    # czy symulujemy zasięg sieci 2G
        self.sms_mode = False     # czy jesteśmy w trybie wpisywania treści SMS (po AT+CMGS)
        self.sms_body = bytearray()
        self.lock = threading.Lock()

        self.ri_gpio = None
        if ri_pin is not None:
            try:
                import RPi.GPIO as GPIO
                GPIO.setmode(GPIO.BCM)
                GPIO.setup(ri_pin, GPIO.OUT, initial=GPIO.HIGH)  # RI w spoczynku: stan wysoki
                self.ri_gpio = (GPIO, ri_pin)
                print(f"[sim] Pin RI/RING: BCM {ri_pin} (stan wysoki)")
            except Exception as e:  # brak RPi.GPIO poza RPi itp.
                print(f"[sim] Nie udało się skonfigurować GPIO {ri_pin}: {e}")
                print("[sim] Kontynuuję bez sterowania pinem RI")

    # ------------------------------------------------------------------ wysyłanie
    def send_line(self, text: str):
        """Wyślij linię zakończoną CRLF (tak formatuje odpowiedzi SIM800L)."""
        with self.lock:
            self.ser.write(f"\r\n{text}\r\n".encode())
        print(f"[sim ->] {text}")

    def send_raw(self, data: bytes):
        with self.lock:
            self.ser.write(data)

    # ------------------------------------------------------------------ komendy AT
    def handle_command(self, line: str):
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
        elif upper == "ATH":
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
        elif upper.startswith("AT+CMGS="):
            # tryb SMS: znak zachęty '>' i zbieranie treści do Ctrl+Z (0x1A)
            self.sms_mode = True
            self.sms_body = bytearray()
            self.send_raw(b"\r\n> ")
            print("[sim] Tryb SMS - czekam na treść zakończoną Ctrl+Z")
        elif upper.startswith("AT+SAPBR=2,1"):
            self.send_line('+SAPBR: 1,1,"10.89.193.1"')
            self.send_line("OK")
        elif upper.startswith("AT+CIPGSMLOC"):
            # format: +CIPGSMLOC: <kod>,<długość>,<szerokość>,<data>,<czas>
            now = time.strftime("%Y/%m/%d,%H:%M:%S", time.gmtime())
            self.send_line(f"+CIPGSMLOC: 0,{FAKE_LONGITUDE},{FAKE_LATITUDE},{now}")
            self.send_line("OK")
        else:
            # pozostałe komendy (CMGF, CMGDA, SAPBR=3/1/0, CSCLK, CFUN, CLIP,
            # CREG=0, IPR, AT&W, ...) kwitujemy OK - tak jak prawdziwy moduł
            self.send_line("OK")

    # ------------------------------------------------------------------ treść SMS
    def handle_sms_byte(self, byte: int):
        if byte == 0x1A:  # Ctrl+Z kończy SMS
            body = self.sms_body.decode(errors="replace")
            print("=" * 60)
            print("[sim] ODEBRANO SMS:")
            print(body)
            print("=" * 60)
            self.sms_mode = False
            time.sleep(0.5)
            self.send_line("+CMGS: 1")
            self.send_line("OK")
        else:
            self.sms_body.append(byte)

    # ------------------------------------------------------------------ zdarzenia
    def simulate_ring(self):
        """Symuluj przychodzące połączenie głosowe (jak po zadzwonieniu na tracker)."""
        print("[sim] Symuluję przychodzące połączenie...")

        def ri_low(duration: float):
            if self.ri_gpio:
                gpio, pin = self.ri_gpio
                gpio.output(pin, gpio.LOW)   # RI aktywne = stan niski
                time.sleep(duration)
                gpio.output(pin, gpio.HIGH)
            else:
                print("[sim] (brak --ri-pin: zewrzyj teraz PD2 do GND na ~2s,")
                print("[sim]  inaczej firmware nie zacznie czytać komunikatu RING)")
                time.sleep(duration)

        # RI ściągnięte do stanu niskiego równolegle z komunikatami URC
        t = threading.Thread(target=ri_low, args=(2.0,), daemon=True)
        t.start()
        time.sleep(0.3)
        self.send_line("RING")
        self.send_line(f'+CLIP: "{FAKE_PHONE}",145,"",0,"",0')
        t.join()

    # ------------------------------------------------------------------ pętle
    def serial_loop(self):
        """Czytaj bajty z UART: linie komend AT albo treść SMS."""
        line_buf = bytearray()
        while True:
            data = self.ser.read(64)
            for byte in data:
                if self.sms_mode:
                    self.handle_sms_byte(byte)
                elif byte in (0x0A, 0x0D):
                    if line_buf:
                        self.handle_command(line_buf.decode(errors="replace"))
                        line_buf = bytearray()
                else:
                    line_buf.append(byte)

    def console_loop(self):
        """Komendy interaktywne z klawiatury."""
        print("[sim] Komendy: ring | noreg | reg | quit")
        while True:
            try:
                cmd = input().strip().lower()
            except EOFError:
                return
            if cmd == "ring":
                self.simulate_ring()
            elif cmd == "noreg":
                self.registered = False
                print("[sim] Symuluję brak zasięgu (+CREG: 0,2)")
            elif cmd == "reg":
                self.registered = True
                print("[sim] Zasięg przywrócony (+CREG: 0,1)")
            elif cmd == "quit":
                return
            elif cmd:
                print("[sim] Nieznana komenda. Dostępne: ring | noreg | reg | quit")


def main():
    parser = argparse.ArgumentParser(description="Symulator SIM800L dla testów trackera")
    parser.add_argument("--port", default="/dev/ttyS0", help="port szeregowy (domyślnie /dev/ttyS0)")
    parser.add_argument("--baud", type=int, default=9600, help="prędkość (domyślnie 9600)")
    parser.add_argument("--ri-pin", type=int, default=None,
                        help="pin BCM RPi podłączony do PD2 AVR - symulacja linii RI/RING")
    args = parser.parse_args()

    sim = Sim800lSimulator(args.port, args.baud, args.ri_pin)
    print(f"[sim] Symulator SIM800L na {args.port} @ {args.baud} bps")

    reader = threading.Thread(target=sim.serial_loop, daemon=True)
    reader.start()
    try:
        sim.console_loop()
    except KeyboardInterrupt:
        pass
    finally:
        if sim.ri_gpio:
            sim.ri_gpio[0].cleanup()
    print("[sim] Koniec")


if __name__ == "__main__":
    main()
