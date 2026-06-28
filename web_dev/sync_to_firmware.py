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


def load_env():
    env = {}
    env_path = os.path.join(DIR, ".env")
    if os.path.exists(env_path):
        with open(env_path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    env[k.strip()] = v.strip()
    return env
SRC_DIR = os.path.join(DIR, "..", "src")
CPP_FILE = os.path.join(SRC_DIR, "config_portal.cpp")


def read_html(filename, env):
    with open(os.path.join(DIR, filename)) as f:
        html = f.read()
    # Substitute build-time env vars directly (not as %VAR% runtime placeholders)
    for key, value in env.items():
        html = html.replace(f"{{{{{key}}}}}", value)
    # Convert remaining {{VAR}} placeholders to ESP32 %VAR% runtime format
    html = re.sub(r"\{\{(\w+)\}\}", r"%\1%", html)
    return html


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
    env = load_env()
    if not env.get("GOOGLE_MAPS_API_KEY"):
        print("WARNING: GOOGLE_MAPS_API_KEY not set in .env — API key will be empty in firmware")

    with open(CPP_FILE) as f:
        cpp = f.read()

    wifi_html = read_html("wifi_setup.html", env)
    dashboard_html = read_html("dashboard.html", env)
    success_html = read_html("success.html", env)

    cpp = update_progmem(cpp, "WIFI_HTML", wifi_html)
    cpp = update_progmem(cpp, "DASHBOARD_HTML", dashboard_html)
    cpp = update_progmem(cpp, "SUCCESS_HTML", success_html)

    with open(CPP_FILE, "w") as f:
        f.write(cpp)

    print("Synced HTML back to src/config_portal.cpp")


if __name__ == "__main__":
    main()
