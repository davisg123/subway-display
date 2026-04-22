#include "config_portal.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

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

static const char* CONFIG_PATH = "/config.json";

// Default configuration
static const double DEFAULT_LAT = 40.706565;
static const double DEFAULT_LON = -74.011333;
static const int DEFAULT_LIMIT = 20;

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

    <button type="submit" class="btn">Connect &amp; Continue</button>
    <button type="submit" class="skip-btn" formnovalidate onclick="document.querySelector('[name=password]').value=''">No password? Connect to open network</button>
  </form>
</body>
</html>
)rawliteral";

// HTML for the full config wizard (served at sign.local after WiFi connected)
static const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Subway Sign Setup</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: -apple-system, sans-serif; max-width: 420px; margin: 0 auto; padding: 20px; background: #f5f5f7; color: #1d1d1f; min-height: 100vh; }
    h1 { color: #0071e3; font-size: 22px; margin: 0 0 4px 0; }
    .subtitle { color: #86868b; font-size: 14px; margin-bottom: 30px; }
    .step-dots { display: flex; gap: 8px; margin-bottom: 30px; }
    .dot { width: 10px; height: 10px; border-radius: 50%; background: #d2d2d7; transition: background 0.3s; }
    .dot.active { background: #0071e3; }
    .dot.done { background: #34c759; }
    .page { display: none; animation: fadeIn 0.3s ease; }
    .page.active { display: block; }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(10px); } to { opacity: 1; transform: translateY(0); } }
    label { color: #6e6e73; font-size: 13px; text-transform: uppercase; letter-spacing: 0.5px; display: block; margin-bottom: 6px; }
    input[type="text"], input[type="number"] {
      width: 100%; padding: 14px; margin: 0 0 20px 0; border: 1px solid #d2d2d7; border-radius: 10px;
      background: #fff; color: #1d1d1f; font-size: 18px; transition: border-color 0.2s;
    }
    input:focus { border-color: #0071e3; outline: none; box-shadow: 0 0 0 3px rgba(0,113,227,0.15); }
    input::placeholder { color: #aeaeb2; }
    .btn { padding: 14px 28px; border: none; border-radius: 10px; cursor: pointer; font-size: 16px; font-weight: 600; transition: all 0.2s; }
    .btn-primary { background: #0071e3; color: #fff; }
    .btn-primary:hover { background: #0077ed; transform: translateY(-1px); }
    .btn-secondary { background: #e8e8ed; color: #1d1d1f; }
    .btn-secondary:hover { background: #d2d2d7; }
    .btn-row { display: flex; gap: 10px; margin-top: 10px; }
    .btn-row .btn-primary { flex: 1; }
    #map { width: 100%; height: 260px; border-radius: 10px; margin: 0 0 12px 0; border: 1px solid #d2d2d7; }
    #map-section { display: none; }
    .coords { color: #aeaeb2; font-size: 12px; margin-bottom: 20px; }
    .page-icon { font-size: 40px; margin-bottom: 16px; display: block; }
    .page-title { font-size: 20px; font-weight: 600; color: #1d1d1f; margin-bottom: 6px; }
    .page-desc { color: #6e6e73; font-size: 14px; margin-bottom: 24px; line-height: 1.5; }
    .pac-container { background: #fff !important; border: 1px solid #d2d2d7 !important; border-radius: 0 0 10px 10px !important; box-shadow: 0 4px 16px rgba(0,0,0,0.1) !important; margin-top: -2px !important; }
    .pac-item { background: #fff !important; color: #1d1d1f !important; border-top: 1px solid #f0f0f0 !important; padding: 10px 14px !important; cursor: pointer !important; line-height: 1.4 !important; }
    .pac-item:hover, .pac-item-selected { background: #f5f5f7 !important; }
    .pac-item-query { color: #1d1d1f !important; font-weight: 500 !important; }
    .pac-matched { color: #0071e3 !important; font-weight: 600 !important; }
    .pac-icon { display: none !important; }
    .review-item { display: flex; justify-content: space-between; align-items: center; padding: 14px 0; border-bottom: 1px solid #e8e8ed; }
    .review-label { color: #86868b; font-size: 13px; }
    .review-value { color: #1d1d1f; font-size: 15px; font-weight: 500; }
    .review-edit { color: #0071e3; background: none; border: none; font-size: 13px; cursor: pointer; font-weight: 500; }

    /* Station selection */
    .stations-toolbar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }
    .stations-toolbar span { color: #86868b; font-size: 13px; }
    .stations-quick-btns { display: flex; gap: 12px; }
    .quick-btn { background: none; border: none; color: #0071e3; font-size: 13px; font-weight: 500; cursor: pointer; padding: 0; }
    .station-card { display: flex; align-items: center; padding: 12px 14px; margin-bottom: 8px; border-radius: 12px; cursor: pointer; transition: background 0.15s; background: #fff; border: 2px solid transparent; user-select: none; }
    .station-card.selected { background: #e8f1fd; border-color: #0071e3; }
    .station-info { flex: 1; }
    .station-name { font-size: 16px; font-weight: 600; color: #1d1d1f; margin-bottom: 6px; }
    .station-routes { display: flex; gap: 5px; margin-bottom: 4px; flex-wrap: wrap; }
    .route-badge { width: 24px; height: 24px; border-radius: 50%; display: inline-flex; align-items: center; justify-content: center; font-size: 11px; font-weight: 700; flex-shrink: 0; }
    .station-dist { color: #aeaeb2; font-size: 12px; }
    .stations-loading { text-align: center; padding: 40px 0; color: #86868b; font-size: 14px; }
    .stations-spinner { width: 32px; height: 32px; border: 3px solid #d2d2d7; border-top-color: #0071e3; border-radius: 50%; animation: spin 0.7s linear infinite; margin: 0 auto 12px; }
    @keyframes spin { to { transform: rotate(360deg); } }
    .stations-error { background: #fff2f2; border: 1px solid #ffc7c7; border-radius: 10px; padding: 16px; text-align: center; color: #c00; font-size: 14px; }
    .stations-error button { margin-top: 10px; background: #c00; color: #fff; border: none; border-radius: 8px; padding: 8px 16px; cursor: pointer; font-size: 13px; }
  </style>
</head>
<body>
  <h1>Subway Sign Setup</h1>
  <p class="subtitle">Step 2 of 2 &mdash; Configure your sign</p>

  <div class="step-dots">
    <div class="dot active" id="dot-0"></div>
    <div class="dot" id="dot-1"></div>
    <div class="dot" id="dot-2"></div>
  </div>

  <form action="/save" method="POST" id="setup-form">

    <!-- Step 1: Location -->
    <div class="page active" id="page-0">
      <div class="page-icon">&#128205;</div>
      <div class="page-title">Your Location</div>
      <div class="page-desc">Search for an address or tap the map to set where your sign is located.</div>
      <div id="map-section">
        <label>Search address</label>
        <input type="text" id="address" placeholder="e.g. 123 Broadway, New York" autocomplete="off">
        <div id="map"></div>
        <div class="coords">Lat: <span id="lat-display">%LAT%</span>, Lon: <span id="lon-display">%LON%</span></div>
      </div>
      <div id="manual-coords" style="display:none">
        <label>Latitude</label>
        <input type="text" id="lat-manual" placeholder="40.706565" value="%LAT%" oninput="updateCoords(parseFloat(this.value)||0, parseFloat(document.getElementById('lon-manual').value)||0)">
        <label>Longitude</label>
        <input type="text" id="lon-manual" placeholder="-74.011333" value="%LON%" oninput="updateCoords(parseFloat(document.getElementById('lat-manual').value)||0, parseFloat(this.value)||0)">
      </div>
      <input type="hidden" name="lat" id="lat" value="%LAT%" required>
      <input type="hidden" name="lon" id="lon" value="%LON%" required>
      <label>Nearby trains to fetch</label>
      <input type="number" name="limit" id="limit" min="1" max="50" value="%LIMIT%">
      <div class="btn-row">
        <button type="button" class="btn btn-primary" onclick="nextPage()">Continue</button>
      </div>
    </div>

    <!-- Step 2: Station Selection -->
    <div class="page" id="page-1">
      <div class="page-icon">&#128644;</div>
      <div class="page-title">Nearby Stations</div>
      <div class="page-desc">Choose which stations to display on your sign.</div>

      <div id="stations-loading" class="stations-loading">
        <div class="stations-spinner"></div>
        Looking up nearby stations&hellip;
      </div>
      <div id="stations-error" class="stations-error" style="display:none">
        Could not load nearby stations.
        <br><button type="button" onclick="fetchStations()">Try again</button>
      </div>
      <div id="stations-list" style="display:none"></div>

      <input type="hidden" name="station_ids" id="station_ids" value="">

      <div class="btn-row" style="margin-top: 24px;">
        <button type="button" class="btn btn-secondary" onclick="prevPage()">Back</button>
        <button type="button" class="btn btn-primary" onclick="nextPage()">Continue</button>
      </div>
    </div>

    <!-- Step 3: Review -->
    <div class="page" id="page-2">
      <div class="page-icon">&#9989;</div>
      <div class="page-title">Review &amp; Save</div>
      <div class="page-desc">Make sure everything looks right before saving.</div>

      <div class="review-item">
        <div><div class="review-label">Location</div><div class="review-value" id="r-location"></div></div>
        <button type="button" class="review-edit" onclick="goToPage(0)">Edit</button>
      </div>
      <div class="review-item">
        <div><div class="review-label">Train Limit</div><div class="review-value" id="r-limit"></div></div>
        <button type="button" class="review-edit" onclick="goToPage(0)">Edit</button>
      </div>
      <div class="review-item">
        <div><div class="review-label">Stations</div><div class="review-value" id="r-stations"></div></div>
        <button type="button" class="review-edit" onclick="goToPage(1)">Edit</button>
      </div>

      <div class="btn-row" style="margin-top: 24px;">
        <button type="button" class="btn btn-secondary" onclick="prevPage()">Back</button>
        <button type="submit" class="btn btn-primary">Save &amp; Restart</button>
      </div>
    </div>
  </form>

  <script>
    var currentPage = 0;
    var totalPages = 3;

    function goToPage(n) {
      document.getElementById('page-' + currentPage).classList.remove('active');
      document.getElementById('page-' + n).classList.add('active');
      for (var i = 0; i < totalPages; i++) {
        var dot = document.getElementById('dot-' + i);
        dot.classList.remove('active', 'done');
        if (i < n) dot.classList.add('done');
        if (i === n) dot.classList.add('active');
      }
      currentPage = n;
      if (n === 2) populateReview();
    }

    function nextPage() {
      if (currentPage === 0) fetchStations();
      if (currentPage < totalPages - 1) goToPage(currentPage + 1);
    }

    function prevPage() {
      if (currentPage > 0) goToPage(currentPage - 1);
    }

    function populateReview() {
      var lat = document.getElementById('lat').value;
      var lon = document.getElementById('lon').value;
      document.getElementById('r-location').textContent = lat + ', ' + lon;
      document.getElementById('r-limit').textContent = document.getElementById('limit').value || '20';

      var total = stationData.length;
      var selected = selectedStations.size;
      if (total === 0) {
        document.getElementById('r-stations').textContent = 'All nearby';
      } else if (selected === total) {
        document.getElementById('r-stations').textContent = 'All ' + total + ' stations';
      } else {
        document.getElementById('r-stations').textContent = selected + ' of ' + total + ' stations';
      }
    }

    // ── Station selection ─────────────────────────────────────────────────────

    var stationData = [];
    var selectedStations = new Set();

    var ROUTE_COLORS = {
      '1': '#EE352E', '2': '#EE352E', '3': '#EE352E',
      '4': '#00933C', '5': '#00933C', '6': '#00933C',
      '7': '#B933AD',
      'A': '#0039A6', 'C': '#0039A6', 'E': '#0039A6',
      'B': '#FF6319', 'D': '#FF6319', 'F': '#FF6319', 'M': '#FF6319',
      'G': '#6CBE45',
      'J': '#996633', 'Z': '#996633',
      'L': '#A7A9AC',
      'N': '#FCCC0A', 'Q': '#FCCC0A', 'R': '#FCCC0A', 'W': '#FCCC0A',
      'S': '#808183'
    };
    var DARK_TEXT_ROUTES = { 'N': 1, 'Q': 1, 'R': 1, 'W': 1, 'L': 1, 'S': 1 };

    function fetchStations() {
      var lat = document.getElementById('lat').value;
      var lon = document.getElementById('lon').value;
      var limit = document.getElementById('limit').value || 20;

      document.getElementById('stations-loading').style.display = 'block';
      document.getElementById('stations-error').style.display = 'none';
      document.getElementById('stations-list').style.display = 'none';

      fetch('/api/stations?lat=' + lat + '&lon=' + lon + '&limit=' + limit)
        .then(function(r) {
          if (!r.ok) throw new Error('HTTP ' + r.status);
          return r.json();
        })
        .then(function(data) {
          stationData = data.stations || [];
          selectedStations = new Set();
          updateStationIds();
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
      var seen = {};
      var out = [];
      var trains = (station.northbound_trains || []).concat(station.southbound_trains || []);
      trains.forEach(function(t) { if (!seen[t.route]) { seen[t.route] = 1; out.push(t.route); } });
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
        var badges = routes.map(function(r) {
          var bg = ROUTE_COLORS[r] || '#808183';
          var color = DARK_TEXT_ROUTES[r] ? '#000' : '#fff';
          return '<span class="route-badge" style="background:' + bg + ';color:' + color + '">' + r + '</span>';
        }).join('');

        html += '<div class="station-card" id="card-' + id + '" onclick="toggleStation(\'' + id + '\')">' +
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
      if (selectedStations.has(id)) {
        selectedStations.delete(id);
        card.classList.remove('selected');
      } else {
        selectedStations.add(id);
        card.classList.add('selected');
      }
      updateStationIds();
    }

    function selectAllStations(select) {
      stationData.forEach(function(s) {
        var id = s.station_id || s.id;
        var card = document.getElementById('card-' + id);
        if (select) {
          selectedStations.add(id);
          if (card) card.classList.add('selected');
        } else {
          selectedStations.delete(id);
          if (card) card.classList.remove('selected');
        }
      });
      updateStationIds();
    }

    function updateStationIds() {
      document.getElementById('station_ids').value = Array.from(selectedStations).join(',');
    }

    // ── Google Maps ───────────────────────────────────────────────────────────

    var map, marker;

    function initMap() {
      var lat = parseFloat(document.getElementById('lat').value) || 40.706565;
      var lng = parseFloat(document.getElementById('lon').value) || -74.011333;
      var pos = { lat: lat, lng: lng };
      document.getElementById('map-section').style.display = 'block';
      document.getElementById('manual-coords').style.display = 'none';

      map = new google.maps.Map(document.getElementById('map'), {
        center: pos, zoom: 15, disableDefaultUI: true, zoomControl: true
      });
      marker = new google.maps.Marker({ position: pos, map: map, draggable: true });
      marker.addListener('dragend', function() {
        updateCoords(marker.getPosition().lat(), marker.getPosition().lng());
      });
      map.addListener('click', function(e) {
        marker.setPosition({ lat: e.latLng.lat(), lng: e.latLng.lng() });
        updateCoords(e.latLng.lat(), e.latLng.lng());
      });

      var input = document.getElementById('address');
      var autocomplete = new google.maps.places.Autocomplete(input, {
        types: ['geocode'], componentRestrictions: { country: 'us' }
      });
      autocomplete.bindTo('bounds', map);
      autocomplete.addListener('place_changed', function() {
        var place = autocomplete.getPlace();
        if (!place.geometry) return;
        var loc = place.geometry.location;
        map.setCenter(loc); map.setZoom(16);
        marker.setPosition(loc);
        updateCoords(loc.lat(), loc.lng());
      });
    }

    function updateCoords(lat, lng) {
      var latStr = lat.toFixed(6);
      var lngStr = lng.toFixed(6);
      document.getElementById('lat').value = latStr;
      document.getElementById('lon').value = lngStr;
      document.getElementById('lat-display').textContent = latStr;
      document.getElementById('lon-display').textContent = lngStr;
    }

    window.gm_authFailure = showManualCoords;
    window.setTimeout(function() {
      if (typeof google === 'undefined') showManualCoords();
    }, 5000);
    function showManualCoords() {
      document.getElementById('map-section').style.display = 'none';
      document.getElementById('manual-coords').style.display = 'block';
    }
  </script>
  <script src="https://maps.googleapis.com/maps/api/js?key=AIzaSyAEEwRPO5vH7aom3xblwKXZu73a_NOC4aw&libraries=places&callback=initMap" async defer></script>
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
    pendingSave = true;

    String html = FPSTR(SUCCESS_HTML);
    request->send(200, "text/html", html);
  });

  server.onNotFound([](AsyncWebServerRequest *request) { request->redirect("/"); });
}

// Connected mode routes: full config wizard at sign.local
static void setupConfigRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = FPSTR(CONFIG_HTML);
    html.replace(F("%LAT%"), String(config.latitude, 6));
    html.replace(F("%LON%"), String(config.longitude, 6));
    html.replace(F("%LIMIT%"), String(config.train_limit));
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

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("lat", true)) {
      config.latitude = request->getParam("lat", true)->value().toDouble();
    }
    if (request->hasParam("lon", true)) {
      config.longitude = request->getParam("lon", true)->value().toDouble();
    }
    if (request->hasParam("limit", true)) {
      config.train_limit = request->getParam("limit", true)->value().toInt();
    }
    if (request->hasParam("station_ids", true)) {
      strncpy(config.station_ids, request->getParam("station_ids", true)->value().c_str(), sizeof(config.station_ids) - 1);
    }
    pendingSave = true;

    String html = FPSTR(SUCCESS_HTML);
    request->send(200, "text/html", html);
  });
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
    restartAt = millis() + 1000;
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
