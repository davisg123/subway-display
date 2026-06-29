#include "config_portal.h"
#include "schedule.h"
#include "version.h"
#include "display.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

// Firmware version (FIRMWARE_VERSION) comes from version.h, injected at build
// time from the git tag. Shown subtly on the dashboard.

// Google Maps key is injected at build time by secrets.py (from web_dev/.env)
// and substituted into the dashboard page at request time — never in source.
#ifndef GOOGLE_MAPS_API_KEY
#define GOOGLE_MAPS_API_KEY ""
#endif

// POSIX timezone for America/New_York (used by the on/off schedule)
static const char* TZ_AMERICA_NEW_YORK = "EST5EDT,M3.2.0,M11.1.0";

// AP configuration
static const char* AP_SSID = "Sign Setup";
static const char* AP_PASSWORD = ""; // Open network for easy setup
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

// mDNS hostname
static const char* MDNS_HOSTNAME = "sign";

// Connection timeout (ms)
static const unsigned long WIFI_CONNECT_TIMEOUT = 15000;

// State
static bool configMode = false;
static DeviceConfig config;
static AsyncWebServer server(80);
static DNSServer dnsServer;
static bool serverStarted = false;
static unsigned long restartAt = 0; // non-zero means restart is pending
static volatile bool pendingSave = false;
static volatile bool pendingRestart = false; // restart after the next deferred save

static const char* CONFIG_PATH = "/config.json";

// Default configuration
static const double DEFAULT_LAT = 40.706565;
static const double DEFAULT_LON = -74.011333;
static const int DEFAULT_LIMIT = 20;
static const int DEFAULT_BRIGHTNESS = 90;  // 0-255

// HTML for AP mode WiFi setup (no internet available)
static const char WIFI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Subway Sign Setup</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: -apple-system, sans-serif; max-width: 420px; margin: 0 auto; padding: 40px 20px; background: #f5f5f7; color: #1d1d1f; min-height: 100vh; }
    h1 { color: #0071e3; font-size: 22px; margin: 0 0 4px 0; }
    .subtitle { color: #86868b; font-size: 14px; margin-bottom: 40px; }
    label { color: #6e6e73; font-size: 13px; text-transform: uppercase; letter-spacing: 0.5px; display: block; margin-bottom: 6px; }
    input[type="text"], input[type="password"] {
      width: 100%; padding: 14px; margin: 0 0 24px 0; border: 1px solid #d2d2d7; border-radius: 10px;
      background: #fff; color: #1d1d1f; font-size: 18px; transition: border-color 0.2s;
    }
    input:focus { border-color: #0071e3; outline: none; box-shadow: 0 0 0 3px rgba(0,113,227,0.15); }
    input::placeholder { color: #aeaeb2; }
    .btn { width: 100%; padding: 16px; border: none; border-radius: 10px; cursor: pointer; font-size: 16px; font-weight: 600; background: #0071e3; color: #fff; transition: background 0.2s; }
    .btn:hover { background: #0077ed; }
    .skip-btn { display: block; text-align: center; background: none; border: none; color: #aeaeb2; font-size: 13px; cursor: pointer; padding: 12px 0 0 0; width: 100%; }
    .skip-btn:hover { color: #6e6e73; }
    .hint { color: #86868b; font-size: 13px; line-height: 1.5; margin-bottom: 24px; padding: 14px; background: #fff; border-radius: 10px; }
    .hint strong { color: #1d1d1f; }
    .bright-row { display: flex; align-items: center; gap: 12px; margin: 0 0 24px 0; }
    .bright-row input[type=range] { flex: 1; -webkit-appearance: none; appearance: none; height: 6px; border-radius: 3px; background: #d2d2d7; outline: none; }
    .bright-row input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 26px; height: 26px; border-radius: 50%; background: #0071e3; cursor: pointer; }
    .bright-row input[type=range]::-moz-range-thumb { width: 26px; height: 26px; border: none; border-radius: 50%; background: #0071e3; cursor: pointer; }
    .bright-val { font-size: 15px; font-weight: 600; width: 44px; text-align: right; }
</style>
</head>
<body>
  <h1>Subway Sign Setup</h1>
  <p class="subtitle">Step 1 of 2 &mdash; Connect to WiFi</p>

  <div class="hint">
    On your iPhone, go to <strong>Settings &rarr; Wi-Fi</strong>. Your current network is shown at the top with a checkmark.
  </div>

  <form action="/save" method="POST">
    <label>Network Name (SSID)</label>
    <input type="text" name="ssid" placeholder="Your WiFi network" value="%SSID%" required autocomplete="off" autocorrect="off" autocapitalize="none">

    <label>Password</label>
    <input type="password" name="password" placeholder="WiFi password" value="%PASSWORD%">

    <label>Brightness</label>
    <div class="bright-row">
      <span>&#9728;</span>
      <input type="range" name="brightness" id="brightness" min="10" max="255" step="5" value="%BRIGHTNESS%"
             oninput="document.getElementById('bright-val').textContent = Math.round(this.value/255*100)+'%'">
      <span class="bright-val" id="bright-val"></span>
    </div>

    <button type="submit" class="btn">Connect &amp; Continue</button>
    <button type="submit" class="skip-btn" formnovalidate onclick="document.querySelector('[name=password]').value=''">No password? Connect to open network</button>
  </form>
  <script>
    (function () {
      var b = document.getElementById('brightness');
      document.getElementById('bright-val').textContent = Math.round(b.value / 255 * 100) + '%';
    })();
  </script>
</body>
</html>
)rawliteral";

// HTML for the connected dashboard (served at sign.local after WiFi connected).
// NOTE: contents are managed by web_dev/sync_to_firmware.py from dashboard.html.
static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Subway Sign</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: -apple-system, sans-serif; max-width: 420px; margin: 0 auto; padding: 20px; background: #f5f5f7; color: #1d1d1f; min-height: 100vh; }
    .header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 20px; }
    h1 { color: #0071e3; font-size: 22px; margin: 0; }
    .status-pill { display: inline-flex; align-items: center; gap: 6px; font-size: 13px; color: #34c759; font-weight: 500; }
    .status-pill .led { width: 8px; height: 8px; border-radius: 50%; background: #34c759; }
    .card { background: #fff; border-radius: 16px; padding: 20px; margin-bottom: 18px; box-shadow: 0 1px 3px rgba(0,0,0,0.06); }
    .card-title { font-size: 13px; text-transform: uppercase; letter-spacing: 0.5px; color: #86868b; margin-bottom: 16px; }

    /* Tabs */
    .tabs { display: flex; gap: 4px; background: #e8e8ed; border-radius: 12px; padding: 4px; margin-bottom: 18px; }
    .tab { flex: 1; text-align: center; padding: 9px 4px; border: none; background: none; border-radius: 9px; font-size: 13px; font-weight: 600; color: #6e6e73; cursor: pointer; transition: all 0.15s; }
    .tab.active { background: #fff; color: #0071e3; box-shadow: 0 1px 2px rgba(0,0,0,0.08); }
    .tab-panel { display: none; }
    .tab-panel.active { display: block; animation: fadeIn 0.25s ease; }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(6px); } to { opacity: 1; transform: translateY(0); } }

    /* Power card */
    .power { display: flex; flex-direction: column; align-items: center; text-align: center; }
    .power-state { font-size: 26px; font-weight: 700; margin-bottom: 4px; }
    .power-state.on { color: #34c759; }
    .power-state.off { color: #86868b; }
    .power-sub { font-size: 13px; color: #86868b; margin-bottom: 20px; }
    .big-switch { position: relative; width: 132px; height: 72px; }
    .big-switch input { opacity: 0; width: 0; height: 0; }
    .big-switch .track { position: absolute; inset: 0; background: #d2d2d7; border-radius: 72px; transition: background 0.25s; cursor: pointer; }
    .big-switch .knob { position: absolute; height: 64px; width: 64px; left: 4px; top: 4px; background: #fff; border-radius: 50%; transition: transform 0.25s; box-shadow: 0 2px 6px rgba(0,0,0,0.25); }
    .big-switch input:checked + .track { background: #34c759; }
    .big-switch input:checked + .track .knob { transform: translateX(60px); }
    .auto-row { display: flex; align-items: center; justify-content: space-between; margin-top: 22px; width: 100%; padding-top: 18px; border-top: 1px solid #e8e8ed; }
    .auto-label { font-size: 15px; font-weight: 500; }
    .auto-hint { font-size: 12px; color: #86868b; margin-top: 2px; }

    /* iOS-style toggle */
    .toggle { position: relative; width: 44px; height: 26px; flex-shrink: 0; }
    .toggle input { opacity: 0; width: 0; height: 0; }
    .toggle .slider { position: absolute; inset: 0; background: #d2d2d7; border-radius: 26px; transition: 0.2s; cursor: pointer; }
    .toggle .slider::before { content: ""; position: absolute; height: 22px; width: 22px; left: 2px; top: 2px; background: #fff; border-radius: 50%; transition: 0.2s; box-shadow: 0 1px 3px rgba(0,0,0,0.2); }
    .toggle input:checked + .slider { background: #34c759; }
    .toggle input:checked + .slider::before { transform: translateX(18px); }

    /* Brightness slider */
    .bright-row { display: flex; align-items: center; gap: 14px; }
    .bright-icon { font-size: 18px; line-height: 1; }
    .bright-row input[type=range] { flex: 1; -webkit-appearance: none; appearance: none; height: 6px; border-radius: 3px; background: #d2d2d7; outline: none; }
    .bright-row input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 26px; height: 26px; border-radius: 50%; background: #fff; box-shadow: 0 1px 4px rgba(0,0,0,0.3); cursor: pointer; }
    .bright-row input[type=range]::-moz-range-thumb { width: 26px; height: 26px; border: none; border-radius: 50%; background: #fff; box-shadow: 0 1px 4px rgba(0,0,0,0.3); cursor: pointer; }
    .bright-val { font-size: 15px; font-weight: 600; color: #1d1d1f; width: 44px; text-align: right; }

    /* Schedule */
    .sched-toolbar { display: flex; justify-content: flex-end; margin-bottom: 4px; }
    .quick-btn { background: none; border: none; color: #0071e3; font-size: 13px; font-weight: 500; cursor: pointer; padding: 0; }
    .sched-row { display: flex; align-items: center; gap: 10px; padding: 10px 0; border-bottom: 1px solid #e8e8ed; }
    .sched-row:last-child { border-bottom: none; }
    .sched-day { flex: 1; display: flex; align-items: center; gap: 8px; min-width: 0; }
    .sched-day-name { font-size: 15px; font-weight: 500; color: #1d1d1f; }
    .sched-times { display: flex; align-items: center; gap: 6px; }
    .sched-times input[type="time"] { width: 104px; padding: 8px; margin: 0; border: 1px solid #d2d2d7; border-radius: 8px; background: #fff; color: #1d1d1f; font-size: 15px; }
    .sched-row.off .sched-times { opacity: 0.35; pointer-events: none; }
    .sched-times .sep { color: #aeaeb2; font-size: 13px; }
    .sched-disabled { opacity: 0.4; pointer-events: none; }

    /* Location */
    label { color: #6e6e73; font-size: 13px; text-transform: uppercase; letter-spacing: 0.5px; display: block; margin-bottom: 6px; }
    input[type="text"], input[type="number"] { width: 100%; padding: 14px; margin: 0 0 16px 0; border: 1px solid #d2d2d7; border-radius: 10px; background: #fff; color: #1d1d1f; font-size: 17px; }
    input:focus { border-color: #0071e3; outline: none; box-shadow: 0 0 0 3px rgba(0,113,227,0.15); }
    #map { width: 100%; height: 240px; border-radius: 10px; margin: 0 0 12px 0; border: 1px solid #d2d2d7; }
    #map-section { display: none; }
    .coords { color: #aeaeb2; font-size: 12px; margin-bottom: 16px; }
    .pac-container { background: #fff !important; border: 1px solid #d2d2d7 !important; border-radius: 0 0 10px 10px !important; box-shadow: 0 4px 16px rgba(0,0,0,0.1) !important; margin-top: -2px !important; }
    .pac-item { background: #fff !important; color: #1d1d1f !important; border-top: 1px solid #f0f0f0 !important; padding: 10px 14px !important; cursor: pointer !important; line-height: 1.4 !important; }
    .pac-item:hover, .pac-item-selected { background: #f5f5f7 !important; }

    /* Stations */
    .stations-toolbar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }
    .stations-toolbar span { color: #86868b; font-size: 13px; }
    .stations-quick-btns { display: flex; gap: 12px; }
    .station-card { display: flex; align-items: center; padding: 12px 14px; margin-bottom: 8px; border-radius: 12px; cursor: pointer; transition: background 0.15s; background: #f5f5f7; border: 2px solid transparent; user-select: none; }
    .station-card.selected { background: #e8f1fd; border-color: #0071e3; }
    .station-info { flex: 1; }
    .station-name { font-size: 16px; font-weight: 600; color: #1d1d1f; margin-bottom: 6px; }
    .station-routes { display: flex; gap: 5px; margin-bottom: 4px; flex-wrap: wrap; }
    .route-badge { width: 24px; height: 24px; border-radius: 50%; display: inline-flex; align-items: center; justify-content: center; font-size: 11px; font-weight: 700; flex-shrink: 0; }
    .station-dist { color: #aeaeb2; font-size: 12px; }
    .stations-loading { text-align: center; padding: 30px 0; color: #86868b; font-size: 14px; }
    .stations-spinner { width: 32px; height: 32px; border: 3px solid #d2d2d7; border-top-color: #0071e3; border-radius: 50%; animation: spin 0.7s linear infinite; margin: 0 auto 12px; }
    @keyframes spin { to { transform: rotate(360deg); } }
    .stations-error { background: #fff2f2; border: 1px solid #ffc7c7; border-radius: 10px; padding: 16px; text-align: center; color: #c00; font-size: 14px; }
    .stations-error button { margin-top: 10px; background: #c00; color: #fff; border: none; border-radius: 8px; padding: 8px 16px; cursor: pointer; font-size: 13px; }

    .btn { width: 100%; padding: 14px; border: none; border-radius: 10px; cursor: pointer; font-size: 16px; font-weight: 600; background: #0071e3; color: #fff; transition: all 0.2s; margin-top: 16px; }
    .btn:hover { background: #0077ed; }
    .saved-note { text-align: center; font-size: 13px; color: #34c759; height: 16px; margin-top: 8px; }
    .fw-version { text-align: center; font-size: 11px; color: #c7c7cc; margin-top: 8px; }
  </style>
</head>
<body>
  <div class="header">
    <h1>Subway Sign</h1>
    <span class="status-pill"><span class="led"></span>Connected</span>
  </div>

  <div class="tabs">
    <button type="button" class="tab active" id="tab-power" onclick="showTab('power')">Power</button>
    <button type="button" class="tab" id="tab-location" onclick="showTab('location')">Location</button>
    <button type="button" class="tab" id="tab-stations" onclick="showTab('stations')">Stations</button>
  </div>

  <!-- Power & Schedule -->
  <div class="tab-panel active" id="panel-power">
    <div class="card">
      <div class="power">
        <div class="power-state" id="power-state">&mdash;</div>
        <div class="power-sub" id="power-sub"></div>
        <label class="big-switch" id="big-switch">
          <input type="checkbox" id="power-toggle" onchange="onBigSwitch(this.checked)">
          <span class="track"><span class="knob"></span></span>
        </label>
        <div class="auto-row">
          <div>
            <div class="auto-label">Automatic schedule</div>
            <div class="auto-hint">Follow the on/off times below</div>
          </div>
          <label class="toggle"><input type="checkbox" id="auto-toggle" onchange="onAutoToggle(this.checked)"><span class="slider"></span></label>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Brightness</div>
      <div class="bright-row">
        <span class="bright-icon">☀</span>
        <input type="range" id="brightness" min="10" max="255" step="5" value="90"
               oninput="onBrightnessInput(this.value)" onchange="onBrightnessCommit(this.value)">
        <span class="bright-val" id="bright-val">&mdash;</span>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Schedule</div>
      <div class="sched-toolbar">
        <button type="button" class="quick-btn" onclick="applyScheduleToAll()">Apply Monday to all days</button>
      </div>
      <div id="schedule-list"></div>
      <button type="button" class="btn" onclick="saveSchedule()">Save schedule</button>
      <div class="saved-note" id="sched-saved"></div>
    </div>
  </div>

  <!-- Location -->
  <div class="tab-panel" id="panel-location">
    <div class="card">
      <div class="card-title">Location</div>
      <div id="map-section">
        <label>Search address</label>
        <input type="text" id="address" placeholder="e.g. 123 Broadway, New York" autocomplete="off">
        <div id="map"></div>
        <div class="coords">Lat: <span id="lat-display"></span>, Lon: <span id="lon-display"></span></div>
      </div>
      <div id="manual-coords" style="display:none">
        <label>Latitude</label>
        <input type="text" id="lat-manual" placeholder="40.706565" oninput="updateCoords(parseFloat(this.value)||0, parseFloat(document.getElementById('lon-manual').value)||0)">
        <label>Longitude</label>
        <input type="text" id="lon-manual" placeholder="-74.011333" oninput="updateCoords(parseFloat(document.getElementById('lat-manual').value)||0, parseFloat(this.value)||0)">
      </div>
      <input type="hidden" id="lat" value="">
      <input type="hidden" id="lon" value="">
      <label>Nearby trains to fetch</label>
      <input type="number" id="limit" min="1" max="50" value="20">
      <button type="button" class="btn" onclick="saveLocation()">Save location</button>
      <div class="saved-note" id="loc-saved"></div>
    </div>
  </div>

  <!-- Stations -->
  <div class="tab-panel" id="panel-stations">
    <div class="card">
      <div class="card-title">Subway Stops</div>
      <div id="stations-loading" class="stations-loading" style="display:none">
        <div class="stations-spinner"></div>
        Looking up nearby stations&hellip;
      </div>
      <div id="stations-error" class="stations-error" style="display:none">
        Could not load nearby stations.
        <br><button type="button" onclick="fetchStations()">Try again</button>
      </div>
      <div id="stations-list" style="display:none"></div>
      <button type="button" class="btn" onclick="saveStations()">Save stations</button>
      <div class="saved-note" id="stations-saved"></div>
    </div>
  </div>

  <div class="fw-version" id="fw-version"></div>

  <script>
    var DAY_NAMES = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
    var DAY_ORDER = [1, 2, 3, 4, 5, 6, 0];

    var schedule = {};
    for (var d = 0; d < 7; d++) schedule[d] = { enabled: true, on: '06:00', off: '00:00' };
    var mode = 'auto';
    var currentOn = false;
    var activeTab = 'power';
    var savedStationIds = '';

    var ROUTE_COLORS = {
      '1': '#EE352E', '2': '#EE352E', '3': '#EE352E',
      '4': '#00933C', '5': '#00933C', '6': '#00933C',
      '7': '#B933AD',
      'A': '#0039A6', 'C': '#0039A6', 'E': '#0039A6',
      'B': '#FF6319', 'D': '#FF6319', 'F': '#FF6319', 'M': '#FF6319',
      'G': '#6CBE45', 'J': '#996633', 'Z': '#996633', 'L': '#A7A9AC',
      'N': '#FCCC0A', 'Q': '#FCCC0A', 'R': '#FCCC0A', 'W': '#FCCC0A', 'S': '#808183'
    };
    var DARK_TEXT_ROUTES = { 'N': 1, 'Q': 1, 'R': 1, 'W': 1, 'L': 1, 'S': 1 };

    // ── Tabs ────────────────────────────────────────────────────────────────
    function showTab(name) {
      activeTab = name;
      ['power', 'location', 'stations'].forEach(function(t) {
        document.getElementById('tab-' + t).classList.toggle('active', t === name);
        document.getElementById('panel-' + t).classList.toggle('active', t === name);
      });
      if (name === 'location') maybeInitMap();
      if (name === 'stations') fetchStations();
    }

    // ── Load ────────────────────────────────────────────────────────────────
    function load() {
      fetch('/api/preferences')
        .then(function(r) { return r.json(); })
        .then(function(data) {
          var p = data.preferences || {};
          if (p.schedule) schedule = p.schedule;
          mode = (data.state && data.state.mode) || 'auto';
          currentOn = !!(data.state && data.state.on);
          if (p.lat) { document.getElementById('lat').value = p.lat; document.getElementById('lat-display').textContent = p.lat; var lm = document.getElementById('lat-manual'); if (lm) lm.value = p.lat; }
          if (p.lon) { document.getElementById('lon').value = p.lon; document.getElementById('lon-display').textContent = p.lon; var om = document.getElementById('lon-manual'); if (om) om.value = p.lon; }
          if (p.limit) document.getElementById('limit').value = p.limit;
          if (p.brightness != null) {
            document.getElementById('brightness').value = p.brightness;
            onBrightnessInput(p.brightness);
          }
          savedStationIds = p.station_ids || '';
          if (data.firmware) document.getElementById('fw-version').textContent = 'Firmware v' + data.firmware;
          renderSchedule();
          renderPower();
        });
    }

    // ── Power ─────────────────────────────────────────────────────────────────
    function renderPower() {
      var stateEl = document.getElementById('power-state');
      var subEl = document.getElementById('power-sub');
      var toggle = document.getElementById('power-toggle');
      var auto = document.getElementById('auto-toggle');

      stateEl.textContent = currentOn ? 'Sign is ON' : 'Sign is OFF';
      stateEl.className = 'power-state ' + (currentOn ? 'on' : 'off');
      auto.checked = mode === 'auto';
      toggle.checked = currentOn;
      toggle.disabled = false;
      subEl.textContent = mode === 'auto'
        ? 'Following schedule — ' + (currentOn ? 'on now' : 'off now')
        : 'Manual override — turn on Automatic to use the schedule';
    }

    // ── Brightness ──────────────────────────────────────────────────────────
    function onBrightnessInput(v) {
      document.getElementById('bright-val').textContent = Math.round(v / 255 * 100) + '%';
    }
    function onBrightnessCommit(v) { postPrefs({ brightness: parseInt(v, 10) }); }

    function onBigSwitch(checked) { setMode(checked ? 'on' : 'off'); }
    function onAutoToggle(checked) { setMode(checked ? 'auto' : (currentOn ? 'on' : 'off')); }
    function setMode(newMode) { mode = newMode; postPrefs({ override: mode }); }

    // ── Schedule ────────────────────────────────────────────────────────────
    function renderSchedule() {
      var list = document.getElementById('schedule-list');
      var html = '';
      DAY_ORDER.forEach(function(day) {
        var s = schedule[day];
        html += '<div class="sched-row' + (s.enabled ? '' : ' off') + '" id="sched-row-' + day + '">' +
          '<div class="sched-day">' +
            '<label class="toggle"><input type="checkbox" ' + (s.enabled ? 'checked' : '') +
              ' onchange="toggleScheduleDay(' + day + ', this.checked)"><span class="slider"></span></label>' +
            '<span class="sched-day-name">' + DAY_NAMES[day] + '</span>' +
          '</div>' +
          '<div class="sched-times">' +
            '<input type="time" value="' + s.on + '" onchange="setScheduleTime(' + day + ', \'on\', this.value)">' +
            '<span class="sep">to</span>' +
            '<input type="time" value="' + s.off + '" onchange="setScheduleTime(' + day + ', \'off\', this.value)">' +
          '</div>' +
        '</div>';
      });
      list.innerHTML = html;
      list.classList.toggle('sched-disabled', mode !== 'auto');
    }

    function toggleScheduleDay(day, enabled) {
      schedule[day].enabled = enabled;
      var row = document.getElementById('sched-row-' + day);
      if (row) row.classList.toggle('off', !enabled);
    }
    function setScheduleTime(day, field, value) { schedule[day][field] = value; }
    function applyScheduleToAll() {
      var monday = schedule[1];
      for (var d = 0; d < 7; d++) schedule[d] = { enabled: monday.enabled, on: monday.on, off: monday.off };
      renderSchedule();
    }
    function saveSchedule() { postPrefs({ schedule: schedule }, 'sched-saved'); }

    // ── Location ──────────────────────────────────────────────────────────────
    var map, marker, googleReady = false, mapInited = false;

    function initMap() { googleReady = true; if (activeTab === 'location') maybeInitMap(); }

    function maybeInitMap() {
      if (!googleReady || mapInited) {
        if (mapInited && map) { google.maps.event.trigger(map, 'resize'); map.setCenter(marker.getPosition()); }
        return;
      }
      mapInited = true;
      document.getElementById('map-section').style.display = 'block';
      document.getElementById('manual-coords').style.display = 'none';
      var lat = parseFloat(document.getElementById('lat').value) || 40.706565;
      var lng = parseFloat(document.getElementById('lon').value) || -74.011333;
      var pos = { lat: lat, lng: lng };
      map = new google.maps.Map(document.getElementById('map'), { center: pos, zoom: 15, disableDefaultUI: true, zoomControl: true });
      marker = new google.maps.Marker({ position: pos, map: map, draggable: true });
      marker.addListener('dragend', function() { updateCoords(marker.getPosition().lat(), marker.getPosition().lng()); });
      map.addListener('click', function(e) { marker.setPosition({ lat: e.latLng.lat(), lng: e.latLng.lng() }); updateCoords(e.latLng.lat(), e.latLng.lng()); });
      var autocomplete = new google.maps.places.Autocomplete(document.getElementById('address'), { types: ['geocode'], componentRestrictions: { country: 'us' } });
      autocomplete.bindTo('bounds', map);
      autocomplete.addListener('place_changed', function() {
        var place = autocomplete.getPlace();
        if (!place.geometry) return;
        var loc = place.geometry.location;
        map.setCenter(loc); map.setZoom(16); marker.setPosition(loc);
        updateCoords(loc.lat(), loc.lng());
      });
    }

    function updateCoords(lat, lng) {
      var latStr = lat.toFixed(6), lngStr = lng.toFixed(6);
      document.getElementById('lat').value = latStr;
      document.getElementById('lon').value = lngStr;
      document.getElementById('lat-display').textContent = latStr;
      document.getElementById('lon-display').textContent = lngStr;
    }

    function saveLocation() {
      postPrefs({
        lat: document.getElementById('lat').value,
        lon: document.getElementById('lon').value,
        limit: document.getElementById('limit').value || '20'
      }, 'loc-saved');
    }

    // Google Maps auth failure / load timeout → fall back to manual lat/lon entry.
    window.gm_authFailure = showManualCoords;
    window.setTimeout(function() { if (typeof google === 'undefined') showManualCoords(); }, 5000);
    function showManualCoords() {
      mapInited = true; // don't try to build the map later
      document.getElementById('map-section').style.display = 'none';
      document.getElementById('manual-coords').style.display = 'block';
    }

    // ── Stations ──────────────────────────────────────────────────────────────
    var stationData = [];
    var selectedStations = new Set();

    function fetchStations() {
      var lat = document.getElementById('lat').value;
      var lon = document.getElementById('lon').value;
      var limit = document.getElementById('limit').value || 20;
      document.getElementById('stations-loading').style.display = 'block';
      document.getElementById('stations-error').style.display = 'none';
      document.getElementById('stations-list').style.display = 'none';

      fetch('/api/stations?lat=' + lat + '&lon=' + lon + '&limit=' + limit)
        .then(function(r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
        .then(function(data) {
          stationData = data.stations || [];
          selectedStations = new Set(savedStationIds ? savedStationIds.split(',').filter(Boolean) : []);
          renderStations();
          document.getElementById('stations-loading').style.display = 'none';
          document.getElementById('stations-list').style.display = 'block';
        })
        .catch(function() {
          document.getElementById('stations-loading').style.display = 'none';
          document.getElementById('stations-error').style.display = 'block';
        });
    }

    function getRoutes(station) {
      var routes = station.routes || [];
      if (routes.length) return routes;
      var seen = {}, out = [];
      (station.northbound_trains || []).concat(station.southbound_trains || []).forEach(function(t) {
        if (!seen[t.route]) { seen[t.route] = 1; out.push(t.route); }
      });
      return out.sort();
    }
    function formatDist(meters) {
      var feet = meters * 3.28084;
      if (feet < 1000) return Math.round(feet) + ' ft';
      return (feet / 5280).toFixed(1) + ' mi';
    }

    function renderStations() {
      var list = document.getElementById('stations-list');
      if (stationData.length === 0) {
        list.innerHTML = '<p style="color:#86868b;text-align:center;padding:24px 0;">No stations found near this location.</p>';
        return;
      }
      var html = '<div class="stations-toolbar">' +
        '<span>' + stationData.length + ' stations found</span>' +
        '<div class="stations-quick-btns">' +
          '<button type="button" class="quick-btn" onclick="selectAllStations(true)">Select all</button>' +
          '<button type="button" class="quick-btn" onclick="selectAllStations(false)">None</button>' +
        '</div></div>';
      stationData.forEach(function(station) {
        var id = station.station_id || station.id;
        var name = station.station_name || station.name;
        var routes = getRoutes(station);
        var sel = selectedStations.has(id);
        var badges = routes.map(function(r) {
          var bg = ROUTE_COLORS[r] || '#808183';
          var color = DARK_TEXT_ROUTES[r] ? '#000' : '#fff';
          return '<span class="route-badge" style="background:' + bg + ';color:' + color + '">' + r + '</span>';
        }).join('');
        html += '<div class="station-card' + (sel ? ' selected' : '') + '" id="card-' + id + '" onclick="toggleStation(\'' + id + '\')">' +
          '<div class="station-info">' +
            '<div class="station-name">' + name + '</div>' +
            '<div class="station-routes">' + badges + '</div>' +
            '<div class="station-dist">' + formatDist(station.distance_meters) + ' away</div>' +
          '</div></div>';
      });
      list.innerHTML = html;
    }

    function toggleStation(id) {
      var card = document.getElementById('card-' + id);
      if (selectedStations.has(id)) { selectedStations.delete(id); card.classList.remove('selected'); }
      else { selectedStations.add(id); card.classList.add('selected'); }
    }
    function selectAllStations(select) {
      stationData.forEach(function(s) {
        var id = s.station_id || s.id;
        var card = document.getElementById('card-' + id);
        if (select) { selectedStations.add(id); if (card) card.classList.add('selected'); }
        else { selectedStations.delete(id); if (card) card.classList.remove('selected'); }
      });
    }
    function saveStations() {
      savedStationIds = Array.from(selectedStations).join(',');
      postPrefs({ station_ids: savedStationIds }, 'stations-saved');
    }

    // ── Persist ───────────────────────────────────────────────────────────────
    function postPrefs(patch, noteId) {
      fetch('/api/preferences', {
        method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(patch)
      })
        .then(function(r) { return r.json(); })
        .then(function(data) {
          mode = (data.state && data.state.mode) || mode;
          currentOn = !!(data.state && data.state.on);
          if (data.preferences && data.preferences.schedule) schedule = data.preferences.schedule;
          renderPower();
          renderSchedule();
          if (noteId) {
            var note = document.getElementById(noteId);
            note.textContent = 'Saved ✓';
            setTimeout(function() { note.textContent = ''; }, 2000);
          }
        });
    }

    // Keep the live state fresh while in automatic mode.
    setInterval(function() {
      if (mode !== 'auto') return;
      fetch('/sign/state').then(function(r) { return r.json(); }).then(function(s) {
        currentOn = !!s.on; renderPower();
      });
    }, 20000);

    load();
  </script>
  <script src="https://maps.googleapis.com/maps/api/js?key=%GOOGLE_MAPS_API_KEY%&libraries=places&callback=initMap" async defer></script>
</body>
</html>
)rawliteral";

static const char SUCCESS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Settings Saved</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: -apple-system, sans-serif; max-width: 420px; margin: 0 auto; padding: 40px 20px; background: #f5f5f7; color: #1d1d1f; text-align: center; }
    h1 { color: #0071e3; font-size: 22px; }
    .card { background: #fff; border-radius: 16px; padding: 28px 24px; margin-top: 24px; }
    .icon { font-size: 48px; margin-bottom: 12px; }
    p { color: #6e6e73; font-size: 15px; line-height: 1.6; margin: 8px 0; }
    strong { color: #1d1d1f; }
  </style>
</head>
<body>
  <h1>Settings Saved!</h1>
  <div class="card">
    <div class="icon">&#9989;</div>
    <p>Your sign is restarting with the new settings.</p>
    <p>It will be available at<br><strong>http://sign.local</strong></p>
  </div>
</body>
</html>
)rawliteral";

// Load configuration from flash
static void loadConfig() {
  // Defaults for all paths below
  config.schedule = defaultSchedule();
  config.power_override = POWER_AUTO;
  config.brightness = DEFAULT_BRIGHTNESS;

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed, using defaults");
    config.latitude = DEFAULT_LAT;
    config.longitude = DEFAULT_LON;
    config.train_limit = DEFAULT_LIMIT;
    return;
  }

  if (LittleFS.exists(CONFIG_PATH)) {
    File f = LittleFS.open(CONFIG_PATH, "r");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (!err) {
      strncpy(config.wifi_ssid,      doc["ssid"]       | "", sizeof(config.wifi_ssid) - 1);
      strncpy(config.wifi_password,  doc["password"]   | "", sizeof(config.wifi_password) - 1);
      config.latitude    = doc["lat"]    | DEFAULT_LAT;
      config.longitude   = doc["lon"]    | DEFAULT_LON;
      config.train_limit = doc["limit"]  | DEFAULT_LIMIT;
      strncpy(config.station_ids,    doc["station_ids"] | "", sizeof(config.station_ids) - 1);
      if (doc["schedule"].is<JsonObject>()) scheduleFromJson(doc["schedule"], config.schedule);
      config.power_override = parsePowerMode(doc["override"] | "auto");
      config.brightness = doc["brightness"] | DEFAULT_BRIGHTNESS;
    } else {
      Serial.printf("Config parse error: %s\n", err.c_str());
    }
  } else {
    Serial.println("No config file, using defaults");
    config.latitude = DEFAULT_LAT;
    config.longitude = DEFAULT_LON;
    config.train_limit = DEFAULT_LIMIT;
  }

  LittleFS.end();
  Serial.printf("Loaded config - SSID: %s, Lat: %.6f, Lon: %.6f\n",
                config.wifi_ssid, config.latitude, config.longitude);
}

// Save configuration to flash
static void saveConfig() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed, cannot save");
    return;
  }

  JsonDocument doc;
  doc["ssid"]       = config.wifi_ssid;
  doc["password"]   = config.wifi_password;
  doc["lat"]        = config.latitude;
  doc["lon"]        = config.longitude;
  doc["limit"]      = config.train_limit;
  doc["station_ids"]= config.station_ids;
  doc["override"]   = powerModeStr(config.power_override);
  doc["brightness"] = config.brightness;
  scheduleToJson(config.schedule, doc["schedule"].to<JsonObject>());

  File f = LittleFS.open(CONFIG_PATH, "w");
  serializeJson(doc, f);
  f.close();
  LittleFS.end();

  Serial.println("Configuration saved to flash");
}

// Process template placeholders
static String processor(const String& var) {
  if (var == "SSID") return String(config.wifi_ssid);
  if (var == "PASSWORD") return String(config.wifi_password);
  if (var == "LAT") return String(config.latitude, 6);
  if (var == "LON") return String(config.longitude, 6);
  if (var == "LIMIT") return String(config.train_limit);
  return String();
}

// Start AP mode for configuration
static void startAPMode() {
  Serial.println("Starting AP mode for configuration...");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  // Start DNS server for captive portal
  dnsServer.start(53, "*", AP_IP);

  Serial.printf("AP started: %s\n", AP_SSID);
  Serial.printf("Connect to WiFi '%s' and go to http://%s\n", AP_SSID, AP_IP.toString().c_str());

  configMode = true;
}

// AP mode routes: WiFi credentials only, reboot on save
static void setupAPRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = FPSTR(WIFI_HTML);
    html.replace(F("%SSID%"), String(config.wifi_ssid));
    html.replace(F("%PASSWORD%"), String(config.wifi_password));
    html.replace(F("%BRIGHTNESS%"), String(config.brightness));
    request->send(200, "text/html", html);
  });

  // Captive portal detection redirects
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/"); });
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/"); });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/"); });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true)) {
      strncpy(config.wifi_ssid, request->getParam("ssid", true)->value().c_str(), sizeof(config.wifi_ssid) - 1);
    }
    if (request->hasParam("password", true)) {
      strncpy(config.wifi_password, request->getParam("password", true)->value().c_str(), sizeof(config.wifi_password) - 1);
    }
    if (request->hasParam("brightness", true)) {
      config.brightness = constrain(request->getParam("brightness", true)->value().toInt(), 0, 255);
    }
    pendingSave = true;
    pendingRestart = true;  // reboot to connect with the new credentials

    String html = FPSTR(SUCCESS_HTML);
    request->send(200, "text/html", html);
  });

  server.onNotFound([](AsyncWebServerRequest *request) { request->redirect("/"); });
}

// Start NTP time sync (needed by the on/off schedule). Non-blocking.
static void initTime() {
  configTzTime(TZ_AMERICA_NEW_YORK, "pool.ntp.org", "time.nist.gov");
  Serial.println("NTP time sync started (America/New_York)");
}

// Fill `out` with current local time; returns false if the clock isn't set yet.
static bool currentLocalTime(struct tm& out) {
  time_t now = time(nullptr);
  localtime_r(&now, &out);
  return (out.tm_year + 1900) >= 2020;
}

// Build the dashboard's preferences + state payload.
static void writePreferencesJson(JsonDocument& doc) {
  JsonObject prefs = doc["preferences"].to<JsonObject>();
  prefs["lat"] = String(config.latitude, 6);
  prefs["lon"] = String(config.longitude, 6);
  prefs["limit"] = config.train_limit;
  prefs["station_ids"] = config.station_ids;
  prefs["override"] = powerModeStr(config.power_override);
  prefs["brightness"] = config.brightness;
  scheduleToJson(config.schedule, prefs["schedule"].to<JsonObject>());

  JsonObject state = doc["state"].to<JsonObject>();
  state["on"] = isSignPoweredOn();
  state["mode"] = powerModeStr(config.power_override);

  doc["firmware"] = FIRMWARE_VERSION;
}

// Connected mode routes: tabbed dashboard at sign.local
static void setupConfigRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = FPSTR(DASHBOARD_HTML);
    html.replace(F("%GOOGLE_MAPS_API_KEY%"), F(GOOGLE_MAPS_API_KEY));
    request->send(200, "text/html", html);
  });

  server.on("/api/stations", HTTP_GET, [](AsyncWebServerRequest *request) {
    String lat = request->hasParam("lat") ? request->getParam("lat")->value() : String(config.latitude, 6);
    String lon = request->hasParam("lon") ? request->getParam("lon")->value() : String(config.longitude, 6);
    String limit = request->hasParam("limit") ? request->getParam("limit")->value() : "20";

    String url = "https://7vwbvo32dk.execute-api.us-east-1.amazonaws.com/stations/nearby?lat=" + lat + "&lon=" + lon + "&limit=" + limit;

    WiFiClientSecure client;
    client.setInsecure(); // skip cert verification — internal API call
    HTTPClient http;
    http.begin(client, url);
    int code = http.GET();
    if (code == 200) {
      request->send(200, "application/json", http.getString());
    } else {
      request->send(502, "application/json", "{\"error\":\"upstream " + String(code) + "\"}");
    }
    http.end();
  });

  // Read current preferences + effective sign state.
  server.on("/api/preferences", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    writePreferencesJson(doc);
    AsyncResponseStream *res = request->beginResponseStream("application/json");
    serializeJson(doc, *res);
    request->send(res);
  });

  // Effective sign state right now (polled by the dashboard).
  server.on("/sign/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["on"] = isSignPoweredOn();
    doc["mode"] = powerModeStr(config.power_override);
    AsyncResponseStream *res = request->beginResponseStream("application/json");
    serializeJson(doc, *res);
    request->send(res);
  });

  // Update preferences live from the dashboard (partial JSON merge, no restart).
  AsyncCallbackJsonWebHandler *prefsHandler = new AsyncCallbackJsonWebHandler(
    "/api/preferences",
    [](AsyncWebServerRequest *request, JsonVariant &json) {
      JsonObject body = json.as<JsonObject>();

      if (!body["lat"].isNull())
        config.latitude = body["lat"].is<const char*>() ? atof(body["lat"]) : body["lat"].as<double>();
      if (!body["lon"].isNull())
        config.longitude = body["lon"].is<const char*>() ? atof(body["lon"]) : body["lon"].as<double>();
      if (!body["limit"].isNull())
        config.train_limit = body["limit"].is<const char*>() ? atoi(body["limit"]) : body["limit"].as<int>();
      if (body["station_ids"].is<const char*>())
        strncpy(config.station_ids, body["station_ids"], sizeof(config.station_ids) - 1);
      if (body["override"].is<const char*>())
        config.power_override = parsePowerMode(body["override"]);
      if (!body["brightness"].isNull()) {
        int b = body["brightness"].is<const char*>() ? atoi(body["brightness"]) : body["brightness"].as<int>();
        config.brightness = constrain(b, 0, 255);
        setDisplayBrightness((uint8_t)config.brightness);
      }
      if (body["schedule"].is<JsonObject>())
        scheduleFromJson(body["schedule"], config.schedule);

      pendingSave = true;  // persist on next loop (no restart)

      JsonDocument doc;
      writePreferencesJson(doc);
      AsyncResponseStream *res = request->beginResponseStream("application/json");
      serializeJson(doc, *res);
      request->send(res);
    });
  prefsHandler->setMethod(HTTP_POST);
  server.addHandler(prefsHandler);
}

void initConfigPortal() {
  loadConfig();
}

bool startWiFi() {
  // Check if we have saved credentials
  if (strlen(config.wifi_ssid) == 0) {
    Serial.println("No WiFi credentials saved, starting AP mode");
    startAPMode();
    setupAPRoutes();
    server.begin();
    serverStarted = true;
    return false;
  }

  // Try to connect with saved credentials
  Serial.printf("Connecting to WiFi: %s\n", config.wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifi_ssid, config.wifi_password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_CONNECT_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Start mDNS
    if (MDNS.begin(MDNS_HOSTNAME)) {
      Serial.printf("mDNS started: http://%s.local\n", MDNS_HOSTNAME);
      MDNS.addService("http", "tcp", 80);
    } else {
      Serial.println("mDNS failed to start");
    }

    initTime();  // start NTP sync for the on/off schedule

    configMode = false;
    return true;
  }

  // Connection failed, start AP mode
  Serial.println("\nWiFi connection failed, starting AP mode");
  WiFi.disconnect();
  startAPMode();
  setupAPRoutes();
  server.begin();
  serverStarted = true;
  return false;
}

void handleConfigPortal() {
  if (pendingSave) {
    pendingSave = false;
    saveConfig();
    if (pendingRestart) {
      pendingRestart = false;
      restartAt = millis() + 1000;
    }
  }
  if (restartAt != 0 && millis() >= restartAt) {
    restartAt = 0;
    ESP.restart();
  }
  if (configMode) {
    dnsServer.processNextRequest();
  }
}

bool isInConfigMode() {
  return configMode;
}

DeviceConfig* getConfig() {
  return &config;
}

void startConfigServer() {
  if (serverStarted) return;

  setupConfigRoutes();
  server.begin();
  serverStarted = true;
  Serial.println("Config server started on port 80");
}

String getApiUrl() {
  char url[768];
  if (strlen(config.station_ids) > 0) {
    snprintf(url, sizeof(url),
             "https://7vwbvo32dk.execute-api.us-east-1.amazonaws.com/trains/nearby?lat=%.6f&lon=%.6f&limit=%d&station_ids=%s",
             config.latitude, config.longitude, config.train_limit, config.station_ids);
  } else {
    snprintf(url, sizeof(url),
             "https://7vwbvo32dk.execute-api.us-east-1.amazonaws.com/trains/nearby?lat=%.6f&lon=%.6f&limit=%d",
             config.latitude, config.longitude, config.train_limit);
  }
  return String(url);
}

bool isSignPoweredOn() {
  if (config.power_override == POWER_ON) return true;
  if (config.power_override == POWER_OFF) return false;
  struct tm ti;
  if (!currentLocalTime(ti)) return true;  // clock not set yet → stay on
  return isSignOn(config.schedule, ti);
}
