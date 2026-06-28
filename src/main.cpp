#include <Arduino.h>
#include <HWCDC.h>
#include <WiFi.h>

HWCDC USBSerial;
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "display.h"
#include "train_parser.h"
#include "kalshi_display.h"
#include "config_portal.h"
#include "ota.h"
const unsigned long POLL_INTERVAL_MS = 60000;
const unsigned long OTA_CHECK_INTERVAL_MS = 3600000;  // 1 hour
unsigned long lastOTACheck = 0;

// Kalshi API configuration
const char* KALSHI_API_URL = "https://api.elections.kalshi.com/trade-api/v2/markets/KXNFLGAME-26JAN18HOUNE-NE";
const unsigned long KALSHI_POLL_INTERVAL_MS = 5000;  // Poll every 5 seconds

// Sports matchup data
SportsMatchup sportsMatchup;
unsigned long lastKalshiPoll = 0;
bool kalshiInitialized = false;

// Background task for non-blocking API requests
TaskHandle_t kalshiFetchTask = NULL;
volatile int newKalshiProb = -1;  // -1 means no new data, 0-100 means new probability
volatile bool fetchInProgress = false;

// Team colors (RGB565) - kept below max brightness so pulse glow is visible
const uint16_t TEXANS_COLOR = 0x9000;    // Texans red (darker so pulse shows)
const uint16_t PATRIOTS_COLOR = 0x0013;  // Patriots blue (darker so pulse shows)

unsigned long lastPollTime = 0;

char titleBuffers[4][TITLE_BUF_LEN];

TrainArrival arrivals[4] = {
  {'?', "Loading...", 0, -1},
  {'?', "Loading...", 0, -1},
  {'?', "Loading...", 0, -1},
  {'?', "Loading...", 0, -1}
};
int arrivalCount = 0;

void parseTrainArrivals(Stream& stream) {
  JsonDocument filter;
  JsonObject trainFilter = filter["stations"][0]["northbound_trains"][0].to<JsonObject>();
  trainFilter["route"] = true;
  trainFilter["direction"] = true;
  trainFilter["minutes_away"] = true;
  filter["stations"][0]["station_name"] = true;
  filter["stations"][0]["southbound_trains"] = filter["stations"][0]["northbound_trains"];

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, stream, DeserializationOption::Filter(filter));

  if (error) {
    Serial.printf("JSON parse error: %s\n", error.c_str());
    return;
  }

  JsonArray stations = doc["stations"];
  if (stations.size() == 0) {
    Serial.println("No stations found");
    return;
  }

  std::vector<ParsedTrain> allTrains;

  for (JsonObject station : stations) {
    std::string stationName = station["station_name"].as<const char*>();

    for (JsonObject train : station["northbound_trains"].as<JsonArray>()) {
      const char* route = train["route"];
      const char* dir = train["direction"];
      allTrains.push_back({route[0], dir[0], stationName, train["minutes_away"]});
    }

    for (JsonObject train : station["southbound_trains"].as<JsonArray>()) {
      const char* route = train["route"];
      const char* dir = train["direction"];
      allTrains.push_back({route[0], dir[0], stationName, train["minutes_away"]});
    }
  }

  Serial.printf("Collected %d trains from %d stations\n", (int)allTrains.size(), (int)stations.size());

  processTrains(allTrains, arrivals, titleBuffers, arrivalCount);

  Serial.printf("Found %d trains for display\n", arrivalCount);
  for (int i = 0; i < arrivalCount; i++) {
    Serial.printf("  %d: %c %s %d min\n", i + 1, arrivals[i].route, arrivals[i].title, arrivals[i].minutesAway);
  }

  setTrainArrivals(arrivals, arrivalCount);
}

// Team name buffers (static storage for string pointers)
static char team1NameBuf[16] = "TEXANS";
static char team2NameBuf[16] = "PATRIOTS";

void initSportsMatchup() {
  // Initialize with default values (Houston Texans vs New England Patriots)
  sportsMatchup.team1.name = team1NameBuf;
  sportsMatchup.team1.probability = 50;
  sportsMatchup.team1.color = TEXANS_COLOR;

  sportsMatchup.team2.name = team2NameBuf;
  sportsMatchup.team2.probability = 50;
  sportsMatchup.team2.color = PATRIOTS_COLOR;
}

// Background task function for fetching Kalshi odds (runs on core 0)
void kalshiFetchTaskFunc(void* parameter) {
  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(KALSHI_API_URL);
      http.addHeader("Accept", "application/json");
      http.setTimeout(4000);  // 4 second timeout

      int httpCode = http.GET();

      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
          int yesBid = doc["market"]["yes_bid"] | 50;
          int houstonProb = 100 - yesBid;
          Serial.printf("Kalshi: Texans %d%% - Patriots %d%%\n", houstonProb, yesBid);
          newKalshiProb = houstonProb;  // Signal main loop with new data
        }
      } else {
        Serial.printf("Kalshi HTTP error: %d\n", httpCode);
      }

      http.end();
    }

    // Wait before next fetch
    vTaskDelay(KALSHI_POLL_INTERVAL_MS / portTICK_PERIOD_MS);
  }
}

void startKalshiFetchTask() {
  xTaskCreatePinnedToCore(
    kalshiFetchTaskFunc,   // Task function
    "KalshiFetch",         // Task name
    8192,                  // Stack size
    NULL,                  // Parameters
    1,                     // Priority (low)
    &kalshiFetchTask,      // Task handle
    0                      // Run on core 0 (WiFi core)
  );
}

void updateKalshiOdds() {
  // Check if background task has new data
  if (newKalshiProb >= 0) {
    int prob = newKalshiProb;
    newKalshiProb = -1;  // Clear the flag

    if (!kalshiInitialized) {
      sportsMatchup.team1.probability = prob;
      sportsMatchup.team2.probability = 100 - prob;
      setSportsMatchup(&sportsMatchup);
      kalshiInitialized = true;
    } else {
      setTargetProbability(prob);
    }
  }
}

void fetchSubwayTimes() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping fetch");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = getApiUrl();
  Serial.printf("Fetching: %s\n", url.c_str());
  http.begin(client, url);

  int httpCode = http.GET();
  Serial.printf("HTTP code: %d\n", httpCode);

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      parseTrainArrivals(http.getStream());
    } else {
      Serial.printf("HTTP error: %d\n", httpCode);
    }
  } else {
    Serial.printf("HTTP request failed: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

void setup() {
  USBSerial.begin(115200);
  delay(2000);  // Give USB time to enumerate
  USBSerial.println("Init.");

  initDisplay();
  initConfigPortal();

  #if LAMP_TEST_ENABLED
  runLampTest();  // Never returns
  #endif

  #if KALSHI_MODE_ENABLED
  Serial.println("Kalshi Sports Mode enabled");
  initKalshiDisplay(getDisplay());
  initSportsMatchup();

  // Show initial display immediately (50/50 until API responds)
  setSportsMatchup(&sportsMatchup);
  kalshiInitialized = true;

  // Connect to WiFi using config portal
  if (!startWiFi()) {
    Serial.println("In AP config mode - connect to Sign Setup WiFi");
    return;  // Stay in config mode
  }

  // Start config server for settings changes
  startConfigServer();

  // Start background task for API polling (non-blocking)
  startKalshiFetchTask();
  return;
  #endif

  // Show loading state
  setTrainArrivals(arrivals, 2);

  // Connect to WiFi using config portal
  if (!startWiFi()) {
    Serial.println("In AP config mode - connect to Sign Setup WiFi");
    return;  // Stay in config mode, loop will handle portal
  }

  // Start config server for settings changes
  startConfigServer();

  showConnectedDisplay(WiFi.localIP().toString().c_str());

  // checkForOTAUpdate();
  // lastOTACheck = millis();

  fetchSubwayTimes();
  lastPollTime = millis();
}

void loop() {
  // Process deferred config saves/restarts (live dashboard saves + AP reboots)
  handleConfigPortal();

  // Handle config portal if in AP mode
  if (isInConfigMode()) {
    updateAPModeDisplay();
    delay(10);
    return;
  }

  #if KALSHI_MODE_ENABLED
  // Poll Kalshi API for updated odds
  updateKalshiOdds();
  // Update sports display animation
  updateSportsDisplay();
  delay(10);
  return;
  #endif

  if (millis() - lastOTACheck >= OTA_CHECK_INTERVAL_MS) {
    checkForOTAUpdate();
    lastOTACheck = millis();
  }

  if (millis() - lastPollTime >= POLL_INTERVAL_MS) {
    fetchSubwayTimes();
    lastPollTime = millis();
  }

  // Apply the on/off schedule (manual override + weekly schedule), then animate
  setDisplayPower(isSignPoweredOn());
  updateDisplay();

  delay(10);  // Small delay for animation smoothness
}