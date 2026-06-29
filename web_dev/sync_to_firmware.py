#!/usr/bin/env python3
"""Sync local HTML templates back into config_portal.cpp PROGMEM strings.

Usage:
    python sync_to_firmware.py

Reads index.html and success.html, converts {{VAR}} placeholders back to
%VAR% (ESP32 template format), and updates the PROGMEM strings in
config_portal.cpp.
"""

import os
import re

DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(DIR, "..", "src")
CPP_FILE = os.path.join(SRC_DIR, "config_portal.cpp")


def read_html(filename):
    with open(os.path.join(DIR, filename)) as f:
        html = f.read()
    # Convert ALL {{VAR}} placeholders to ESP32 %VAR% runtime format.
    # Secrets (e.g. GOOGLE_MAPS_API_KEY) are intentionally NOT baked into the
    # source here — the key is injected into the binary at build time by
    # secrets.py and substituted into the page at request time, so it never
    # lands in committed code.
    return re.sub(r"\{\{(\w+)\}\}", r"%\1%", html)


def update_progmem(cpp_source, var_name, new_html):
    """Replace a PROGMEM string constant in the C++ source."""
    pattern = (
        rf'(static const char {var_name}\[\] PROGMEM = R"rawliteral\()'
        r'(.*?)'
        r'(\)rawliteral";)'
    )
    def replacer(m):
        return m.group(1) + '\n' + new_html + m.group(3)
    result, count = re.subn(pattern, replacer, cpp_source, flags=re.DOTALL)
    if count == 0:
        print(f"WARNING: Could not find {var_name} in config_portal.cpp")
    return result


def main():
    with open(CPP_FILE) as f:
        cpp = f.read()

    wifi_html = read_html("wifi_setup.html")
    dashboard_html = read_html("dashboard.html")
    success_html = read_html("success.html")

    cpp = update_progmem(cpp, "WIFI_HTML", wifi_html)
    cpp = update_progmem(cpp, "DASHBOARD_HTML", dashboard_html)
    cpp = update_progmem(cpp, "SUCCESS_HTML", success_html)

    with open(CPP_FILE, "w") as f:
        f.write(cpp)

    print("Synced HTML back to src/config_portal.cpp")


if __name__ == "__main__":
    main()
