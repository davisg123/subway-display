This project renders NY subway times on a 160×40 LED panel (two 80×40 HUB75 panels chained). The panel dimensions live in `src/display.h` (`PANEL_RES_X` × `PANEL_CHAIN` = width, `PANEL_RES_Y` = height) and are the single source of truth — the simulator (`sim/`) derives its window size from them, so firmware and sim never drift.

For rendering visual effects on the board, reference [text](../../../../dev/ESP32-HUB75-MatrixPanel-DMA/examples)

## Frontend / Config Portal

The device serves two web UIs from the ESP32 over HTTP:

- **AP mode** (`wifi_setup.html`, served when no WiFi is configured): a one-step captive portal for WiFi credentials. Saving reboots the device.
- **Connected dashboard** (`dashboard.html`, served at `http://sign.local`): a tabbed control panel —
  - **Power**: a big on/off switch (manual override) + "Automatic schedule" toggle + a per-day-of-week on/off schedule. Flipping the big switch takes manual control (disables Automatic).
  - **Location**: address search / map (or manual lat·lon fallback) + train limit.
  - **Stations**: pick which nearby stops to show.
  - Firmware version is shown subtly at the bottom.

  The dashboard saves each section live via `POST /api/preferences` (JSON, no reboot); it reads current prefs + effective sign state from `GET /api/preferences` and polls `GET /sign/state`.

### Sign on/off logic

`src/schedule.{h,cpp}` holds the weekly schedule + power override and `isSignOn` / `effectiveSignOn`. `config_portal.cpp` exposes `isSignPoweredOn()` (override wins; otherwise the schedule, evaluated against NTP local time — `America/New_York`, started on WiFi connect; defaults to ON until the clock syncs). `main.cpp` calls `setDisplayPower(isSignPoweredOn())` each loop; `display.cpp` blanks the panel when off (arrivals are retained for when it turns back on).

### Local dev workflow

`web_dev/` contains a standalone dev environment:

- `wifi_setup.html` / `dashboard.html` / `success.html` — the actual HTML templates. Use `{{VAR}}` placeholders (server.py substitutes these at request time). `index.html` is the legacy linear wizard (no longer served by the firmware).
- `server.py` — Python mock server on `http://localhost:8080`. Mimics ESP32 endpoints: `GET /` (dashboard), `GET /wifi` (AP page), `GET|POST /api/preferences`, `GET /sign/state`, `GET /api/stations`, `POST /save`. Persists to `config.json`.
- `sync_to_firmware.py` — converts `{{VAR}}` → `%VAR%`, substitutes build-time env vars from `.env` (e.g. `GOOGLE_MAPS_API_KEY`), and writes the HTML back into the `PROGMEM` strings (`WIFI_HTML`, `DASHBOARD_HTML`, `SUCCESS_HTML`) in `src/config_portal.cpp`.
- `config.json` — local saved state (gitignored).

**To run locally:**
```
cd web_dev
python server.py
# open http://localhost:8080
```

**To sync changes to firmware after editing HTML:**
```
cd web_dev
python sync_to_firmware.py
```

The Location map uses the Google Maps JS API (Places autocomplete + click-to-place marker). The key is injected at sync time from `web_dev/.env` (`GOOGLE_MAPS_API_KEY`); update it there and re-sync if you see `ExpiredKeyMapError`. The dashboard falls back to manual lat/lon entry if the key fails to load.