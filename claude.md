This project renders NY subway times on a 32x128 LED panel

For rendering visual effects on the board, reference [text](../../../../dev/ESP32-HUB75-MatrixPanel-DMA/examples)

## Frontend / Config Portal

The device hosts a multi-step setup wizard (WiFi, location, review) served from the ESP32 over HTTP.

### Local dev workflow

`web_dev/` contains a standalone dev environment:

- `index.html` / `success.html` — the actual HTML templates. Use `{{VAR}}` placeholders (server.py substitutes these at request time).
- `server.py` — Python mock server on `http://localhost:8080`. Mimics ESP32 endpoints (`GET /`, `POST /save`). Persists config to `config.json`.
- `sync_to_firmware.py` — converts `{{VAR}}` → `%VAR%` and writes the HTML back into the `PROGMEM` strings in `src/config_portal.cpp`.
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

The map on step 3 uses the Google Maps JS API (Places autocomplete + click-to-place marker).