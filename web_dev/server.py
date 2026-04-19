#!/usr/bin/env python3
"""Local dev server for the Subway Sign config portal.

Mimics the ESP32 web server endpoints so you can iterate on HTML/CSS
without reflashing. Saves config to a local JSON file.

Usage:
    python server.py
    # then open http://localhost:8080
"""

import json
import os
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import parse_qs

PORT = 8080
DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(DIR, "config.json")

DEFAULT_CONFIG = {
    "ssid": "",
    "password": "",
    "lat": "40.706565",
    "lon": "-74.011333",
    "limit": "20",
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
    return html


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/" or self.path.startswith("/?"):
            config = load_config()
            html = render("index.html", config)
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(html.encode())
        else:
            # Captive portal catch-all: redirect to /
            self.send_response(302)
            self.send_header("Location", "/")
            self.end_headers()

    def do_POST(self):
        if self.path == "/save":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode()
            params = parse_qs(body)

            config = load_config()
            for key in ("ssid", "password", "lat", "lon", "limit"):
                if key in params:
                    config[key] = params[key][0]

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


if __name__ == "__main__":
    server = HTTPServer(("localhost", PORT), Handler)
    print(f"Dev server running at http://localhost:{PORT}")
    print("Edit index.html / success.html and refresh to see changes.")
    print("Press Ctrl+C to stop.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
