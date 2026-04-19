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
    # Convert local {{VAR}} placeholders back to ESP32 %VAR% format
    html = re.sub(r"\{\{(\w+)\}\}", r"%\1%", html)
    return html


def update_progmem(cpp_source, var_name, new_html):
    """Replace a PROGMEM string constant in the C++ source."""
    pattern = (
        rf'(static const char {var_name}\[\] PROGMEM = R"rawliteral\()'
        r'(.*?)'
        r'(\)rawliteral";)'
    )
    replacement = rf'\g<1>\n{new_html}\g<3>'
    result, count = re.subn(pattern, replacement, cpp_source, flags=re.DOTALL)
    if count == 0:
        print(f"WARNING: Could not find {var_name} in config_portal.cpp")
    return result


def main():
    with open(CPP_FILE) as f:
        cpp = f.read()

    config_html = read_html("index.html")
    success_html = read_html("success.html")

    cpp = update_progmem(cpp, "CONFIG_HTML", config_html)
    cpp = update_progmem(cpp, "SUCCESS_HTML", success_html)

    with open(CPP_FILE, "w") as f:
        f.write(cpp)

    print("Synced HTML back to src/config_portal.cpp")


if __name__ == "__main__":
    main()
