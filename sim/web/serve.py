#!/usr/bin/env python3
"""Local dev server for the WASM subway sign.

Serves the static page (index.html, subway_sim.js/.wasm) AND proxies
/trains/nearby to the live API with CORS headers added — the real API has
no CORS headers, so the browser can't read it cross-origin without this.

Usage:
    cd sim/web && python3 serve.py
    # open http://localhost:8000
"""
import http.server
import socketserver
import urllib.request
import urllib.error
import os

PORT = 8000
UPSTREAM = "https://7vwbvo32dk.execute-api.us-east-1.amazonaws.com"

os.chdir(os.path.dirname(os.path.abspath(__file__)))


class Handler(http.server.SimpleHTTPRequestHandler):
    # Required so browsers will instantiate the .wasm via streaming compile.
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
    }

    def do_GET(self):
        if self.path.startswith("/trains/"):
            return self._proxy()
        return super().do_GET()

    def _proxy(self):
        url = UPSTREAM + self.path
        try:
            with urllib.request.urlopen(url, timeout=10) as upstream:
                body = upstream.read()
                self.send_response(upstream.status)
                self.send_header("Content-Type", "application/json")
        except urllib.error.HTTPError as e:
            body = e.read()
            self.send_response(e.code)
            self.send_header("Content-Type", "application/json")
        except Exception as e:
            self.send_response(502)
            self.send_header("Content-Type", "text/plain")
            body = str(e).encode()
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), Handler) as httpd:
        print(f"Serving subway sign on http://localhost:{PORT}")
        httpd.serve_forever()
