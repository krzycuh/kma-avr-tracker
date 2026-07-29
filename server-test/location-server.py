#!/usr/bin/env python3
"""
Przykładowy serwer REST odbierający raporty pozycji z trackera (avr-app/gps-tracker.c).

Tracker wysyła (przez SIM800L, czystym HTTP):

    GET /api/location?token=<API_TOKEN>&lat=<szerokość>&lon=<długość>

Serwer waliduje token, dopisuje pozycję (ze znacznikiem czasu odbioru) do pliku CSV
i wypisuje link do Google Maps. Odpowiada 200 "OK" - to na tę odpowiedź czeka firmware
(+HTTPACTION: 0,200) i po niej miga diodą statusu.

Uruchomienie (na serwerze osiągalnym z publicznego internetu - patrz docs/PLAN.md):

    python3 location-server.py --port 8080 --token twoj-tajny-token --log pozycje.csv

Uwaga: SIM800L nie obsługuje nowoczesnego TLS, więc ruch idzie czystym HTTP.
Token traktuj jako słabe uwierzytelnienie: ma odsiewać przypadkowe/masowe skanowanie,
a nie zatrzymać zdeterminowanego podsłuchującego. Nie używaj tu żadnego swojego hasła.
Tylko standardowa biblioteka Pythona - bez zależności do instalowania.
"""

import argparse
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse


class LocationHandler(BaseHTTPRequestHandler):

    def do_GET(self):
        parsed = urlparse(self.path)
        params = parse_qs(parsed.query)
        token = params.get("token", [""])[0]

        if token != self.server.api_token:
            print(f"[srv] ODRZUCONO (zły token) od {self.client_address[0]}: {self.path}")
            self.send_error_short(403, b"FORBIDDEN")
            return

        lat = params.get("lat", [""])[0]
        lon = params.get("lon", [""])[0]
        if not lat or not lon:
            print(f"[srv] ODRZUCONO (brak wspolrzednych): {self.path}")
            self.send_error_short(400, b"BAD REQUEST")
            return

        stamp = time.strftime("%Y-%m-%d %H:%M:%S")
        with open(self.server.logfile, "a") as f:
            f.write(f"{stamp};{lat};{lon}\n")
        print(f"[srv] {stamp}  pozycja: {lat},{lon}"
              f"  ->  https://maps.google.com/maps?q={lat},{lon}")

        body = b"OK"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_error_short(self, code, body):
        """Krótka odpowiedź błędu (bez domyślnej strony HTML - SIM800L jej nie potrzebuje)."""
        self.send_response(code)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass  # własne, czytelniejsze logi w do_GET


def main():
    parser = argparse.ArgumentParser(description="Serwer REST dla GPS trackera")
    parser.add_argument("--port", type=int, default=8080, help="port nasłuchu (domyślnie 8080)")
    parser.add_argument("--token", default="twoj-tajny-token",
                        help="token autoryzacyjny - ten sam co API_TOKEN w gps-tracker.c")
    parser.add_argument("--log", default="pozycje.csv",
                        help="plik CSV na historię pozycji (domyślnie pozycje.csv)")
    args = parser.parse_args()

    server = HTTPServer(("0.0.0.0", args.port), LocationHandler)
    server.api_token = args.token
    server.logfile = args.log
    print(f"[srv] Nasłuch na porcie {args.port}, log: {args.log}")
    print(f"[srv] Test z przeglądarki/curl: "
          f"http://localhost:{args.port}/api/location?token={args.token}&lat=52.23&lon=21.01")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[srv] Koniec")


if __name__ == "__main__":
    main()
