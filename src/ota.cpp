#include "ota.h"
#include "display.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#define OTA_REPO        "davisgossage/subway_sign"
#define OTA_API_URL     "https://api.github.com/repos/" OTA_REPO "/releases/latest"

static void showOTAMessage(const char* line1, const char* line2) {
  MatrixPanel_I2S_DMA* display = getDisplay();
  display->clearScreen();
  display->setTextSize(1);
  display->setTextColor(display->color565(255, 200, 0));
  display->setCursor(2, 4);
  display->print(line1);
  display->setTextColor(display->color565(0, 255, 255));
  display->setCursor(2, 20);
  display->print(line2);
  display->flipDMABuffer();
}

void checkForOTAUpdate() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, OTA_API_URL);
  http.addHeader("User-Agent", "SubwaySign");
  http.setTimeout(8000);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("OTA check failed: HTTP %d\n", code);
    http.end();
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    Serial.printf("OTA JSON parse error: %s\n", err.c_str());
    return;
  }

  // tag_name is e.g. "v1.0.1" — strip leading 'v'
  String tag = doc["tag_name"].as<String>();
  if (tag.startsWith("v")) tag = tag.substring(1);

  if (tag == FIRMWARE_VERSION) {
    Serial.printf("OTA: up to date (%s)\n", FIRMWARE_VERSION);
    return;
  }

  // Find firmware.bin asset download URL
  String downloadUrl;
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    String name = asset["name"].as<String>();
    if (name == "firmware.bin") {
      downloadUrl = asset["browser_download_url"].as<String>();
      break;
    }
  }

  if (downloadUrl.isEmpty()) {
    Serial.printf("OTA: release %s found but no firmware.bin asset\n", tag.c_str());
    return;
  }

  Serial.printf("OTA: updating %s -> %s\n", FIRMWARE_VERSION, tag.c_str());
  showOTAMessage("Updating firmware", tag.c_str());

  // HTTPUpdate needs a fresh client for the actual download
  WiFiClientSecure dlClient;
  dlClient.setInsecure();

  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return ret = httpUpdate.update(dlClient, downloadUrl);

  switch (ret) {
    case HTTP_UPDATE_OK:
      // rebootOnUpdate(true) means we won't reach here
      break;
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA: failed (%d): %s\n",
        httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      break;
  }
}
