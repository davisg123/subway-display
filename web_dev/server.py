#!/usr/bin/env python3
"""Local dev server for the Subway Sign config portal.

Mimics the ESP32 web server endpoints so you can iterate on HTML/CSS
without reflashing. Saves config to a local JSON file.

Two simulated modes:
  http://localhost:8080/wifi  — AP captive portal (WiFi setup only)
  http://localhost:8080/      — sign.local full config wizard

Usage:
    python server.py
    # then open http://localhost:8080 or http://localhost:8080/wifi
"""

import json
import os
import urllib.request
import urllib.error
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import parse_qs, urlparse


def load_env():
    env = {}
    env_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env")
    if os.path.exists(env_path):
        with open(env_path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    env[k.strip()] = v.strip()
    return env


ENV = load_env()

PORT = 8080
DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(DIR, "config.json")

DEFAULT_CONFIG = {
    "ssid": "",
    "password": "",
    "lat": "40.706565",
    "lon": "-74.011333",
    "limit": "20",
    "station_ids": "",
}


def load_config():
    if os.path.exists(CONFIG_FILE):
        with open(CONFIG_FILE) as f:
            return json.load(f)
    return dict(DEFAULT_CONFIG)


def save_config(cfg):
    with open(CONFIG_FILE, "w") as f:
        json.dump(cfg, f, indent=2)


def render(template_name, config):
    with open(os.path.join(DIR, template_name)) as f:
        html = f.read()
    html = html.replace("{{SSID}}", config.get("ssid", ""))
    html = html.replace("{{PASSWORD}}", config.get("password", ""))
    html = html.replace("{{LAT}}", config.get("lat", ""))
    html = html.replace("{{LON}}", config.get("lon", ""))
    html = html.replace("{{LIMIT}}", config.get("limit", "20"))
    html = html.replace("{{GOOGLE_MAPS_API_KEY}}", ENV.get("GOOGLE_MAPS_API_KEY", ""))
    return html


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/wifi":
            # Simulate AP captive portal WiFi setup page
            config = load_config()
            html = render("wifi_setup.html", config)
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(html.encode())

        elif path == "/" or path == "":
            # Simulate sign.local full config wizard
            config = load_config()
            html = render("index.html", config)
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(html.encode())

        elif path == "/api/stations":
            params = parse_qs(parsed.query)
            lat = params.get("lat", ["40.706565"])[0]
            lon = params.get("lon", ["-74.011333"])[0]
            limit = params.get("limit", ["20"])[0]
            api_url = (
                "https://7vwbvo32dk.execute-api.us-east-1.amazonaws.com"
                f"/stations/nearby?lat={lat}&lon={lon}&limit={limit}"
            )
            print(f"[stations] fetching: {api_url}")
            try:
                with urllib.request.urlopen(api_url, timeout=10) as resp:
                    data = resp.read()
                print(f"[stations] status={resp.status} bytes={len(data)}")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(data)
            except urllib.error.HTTPError as e:
                body = e.read()
                print(f"[stations] HTTPError {e.code}: {body[:500]}")
                self.send_response(502)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"error": f"HTTP {e.code}", "detail": body.decode(errors="replace")}).encode())
            except Exception as e:
                print(f"[stations] error: {type(e).__name__}: {e}")
                self.send_response(502)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"error": str(e)}).encode())

        else:
            self.send_response(302)
            self.send_header("Location", "/")
            self.end_headers()

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode()
        params = parse_qs(body)

        if path == "/save":
            config = load_config()
            # Detect WiFi-only save (from /wifi page) vs full config save
            if "ssid" in params:
                for key in ("ssid", "password"):
                    if key in params:
                        config[key] = params[key][0]
                print(f"[wifi] saved ssid={config.get('ssid')}")
            else:
                for key in ("lat", "lon", "limit", "station_ids"):
                    if key in params:
                        config[key] = params[key][0]
                print(f"Config saved: {json.dumps(config, indent=2)}")
            save_config(config)
            print(f"Config saved: {json.dumps(config, indent=2)}")
            html = render("success.html", config)
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(html.encode())

        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, fmt, *args):
        print(f"[{self.address_string()}] {fmt % args}")


if __name__ == "__main__":
    server = HTTPServer(("localhost", PORT), Handler)
    print(f"Dev server running at http://localhost:{PORT}")
    print(f"  http://localhost:{PORT}/wifi  — AP captive portal (WiFi setup)")
    print(f"  http://localhost:{PORT}/      — sign.local full config wizard")
    print("Press Ctrl+C to stop.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
