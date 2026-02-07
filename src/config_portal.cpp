#include "config_portal.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <DNSServer.h>

// AP configuration
static const char* AP_SSID = "SubwaySign-Setup";
static const char* AP_PASSWORD = ""; // Open network for easy setup
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

// mDNS hostname
static const char* MDNS_HOSTNAME = "subway-sign";

// Connection timeout (ms)
static const unsigned long WIFI_CONNECT_TIMEOUT = 15000;

// State
static bool configMode = false;
static DeviceConfig config;
static Preferences preferences;
static AsyncWebServer server(80);
static DNSServer dnsServer;
static bool serverStarted = false;

// Default configuration
static const double DEFAULT_LAT = 40.706565;
static const double DEFAULT_LON = -74.011333;
static const int DEFAULT_LIMIT = 20;

// HTML for the config portal
static const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Subway Sign Setup</title>
  <style>
    body { font-family: -apple-system, sans-serif; max-width: 400px; margin: 40px auto; padding: 20px; background: #1a1a2e; color: #eee; }
    h1 { color: #00d4ff; font-size: 24px; }
    h2 { color: #888; font-size: 16px; margin-top: 30px; }
    input, select { width: 100%; padding: 12px; margin: 8px 0 16px 0; border: 1px solid #333; border-radius: 8px; box-sizing: border-box; background: #16213e; color: #eee; font-size: 16px; }
    input:focus { border-color: #00d4ff; outline: none; }
    button { background: #00d4ff; color: #1a1a2e; padding: 14px 20px; border: none; border-radius: 8px; cursor: pointer; width: 100%; font-size: 16px; font-weight: bold; }
    button:hover { background: #00b8e6; }
    .info { background: #16213e; padding: 15px; border-radius: 8px; margin-top: 20px; font-size: 14px; }
    .success { background: #1e4620; padding: 15px; border-radius: 8px; margin: 20px 0; }
    label { color: #888; font-size: 14px; }
  </style>
</head>
<body>
  <h1>Subway Sign Setup</h1>

  <form action="/save" method="POST">
    <h2>WiFi Settings</h2>
    <label>Network Name (SSID)</label>
    <input type="text" name="ssid" placeholder="Your WiFi network" value="%SSID%" required>

    <label>Password</label>
    <input type="password" name="password" placeholder="WiFi password" value="%PASSWORD%">

    <h2>Location Settings</h2>
    <label>Latitude</label>
    <input type="text" name="lat" placeholder="40.706565" value="%LAT%" required>

    <label>Longitude</label>
    <input type="text" name="lon" placeholder="-74.011333" value="%LON%" required>

    <label>Number of trains to fetch</label>
    <input type="number" name="limit" min="1" max="50" value="%LIMIT%">

    <button type="submit">Save & Connect</button>
  </form>

  <div class="info">
    After saving, the sign will restart and connect to your WiFi network.
    Access settings at <strong>http://subway-sign.local</strong>
  </div>
</body>
</html>
)rawliteral";

static const char SUCCESS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Settings Saved</title>
  <style>
    body { font-family: -apple-system, sans-serif; max-width: 400px; margin: 40px auto; padding: 20px; background: #1a1a2e; color: #eee; text-align: center; }
    h1 { color: #00d4ff; }
    .success { background: #1e4620; padding: 20px; border-radius: 8px; margin: 20px 0; }
  </style>
</head>
<body>
  <h1>Settings Saved!</h1>
  <div class="success">
    <p>The subway sign is now restarting...</p>
    <p>It will connect to <strong>%SSID%</strong></p>
    <p>Once connected, access it at:<br><strong>http://subway-sign.local</strong></p>
  </div>
</body>
</html>
)rawliteral";

// Load configuration from flash
static void loadConfig() {
  preferences.begin("subway", true); // read-only

  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");

  strncpy(config.wifi_ssid, ssid.c_str(), sizeof(config.wifi_ssid) - 1);
  strncpy(config.wifi_password, password.c_str(), sizeof(config.wifi_password) - 1);
  config.latitude = preferences.getDouble("lat", DEFAULT_LAT);
  config.longitude = preferences.getDouble("lon", DEFAULT_LON);
  config.train_limit = preferences.getInt("limit", DEFAULT_LIMIT);

  preferences.end();

  Serial.printf("Loaded config - SSID: %s, Lat: %.6f, Lon: %.6f\n",
                config.wifi_ssid, config.latitude, config.longitude);
}

// Save configuration to flash
static void saveConfig() {
  preferences.begin("subway", false); // read-write

  preferences.putString("ssid", config.wifi_ssid);
  preferences.putString("password", config.wifi_password);
  preferences.putDouble("lat", config.latitude);
  preferences.putDouble("lon", config.longitude);
  preferences.putInt("limit", config.train_limit);

  preferences.end();

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

// Setup web server routes
static void setupServerRoutes() {
  // Main config page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", CONFIG_HTML, processor);
  });

  // Handle captive portal detection
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });

  // Save configuration
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true)) {
      strncpy(config.wifi_ssid, request->getParam("ssid", true)->value().c_str(), sizeof(config.wifi_ssid) - 1);
    }
    if (request->hasParam("password", true)) {
      strncpy(config.wifi_password, request->getParam("password", true)->value().c_str(), sizeof(config.wifi_password) - 1);
    }
    if (request->hasParam("lat", true)) {
      config.latitude = request->getParam("lat", true)->value().toDouble();
    }
    if (request->hasParam("lon", true)) {
      config.longitude = request->getParam("lon", true)->value().toDouble();
    }
    if (request->hasParam("limit", true)) {
      config.train_limit = request->getParam("limit", true)->value().toInt();
    }

    saveConfig();

    // Send success page with SSID
    String html = String(SUCCESS_HTML);
    html.replace("%SSID%", config.wifi_ssid);
    request->send(200, "text/html", html);

    // Restart after a short delay to let the response be sent
    delay(2000);
    ESP.restart();
  });

  // Catch-all for captive portal
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/");
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
    setupServerRoutes();
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
  setupServerRoutes();
  server.begin();
  serverStarted = true;
  return false;
}

void handleConfigPortal() {
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

  setupServerRoutes();
  server.begin();
  serverStarted = true;
  Serial.println("Config server started on port 80");
}

String getApiUrl() {
  char url[256];
  snprintf(url, sizeof(url),
           "https://7vwbvo32dk.execute-api.us-east-1.amazonaws.com/trains/nearby?lat=%.6f&lon=%.6f&limit=%d",
           config.latitude, config.longitude, config.train_limit);
  return String(url);
}
