#include "ota.h"
#include "display.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#define OTA_REPO        "davisg123/subway-display"
#if OTA_INCLUDE_PRERELEASES
#define OTA_API_URL "https://api.github.com/repos/" OTA_REPO "/releases"
#else
#define OTA_API_URL "https://api.github.com/repos/" OTA_REPO "/releases/latest"
#endif

// Parse "major.minor.patch" (ignoring any trailing pre-release/build suffix).
// Returns false if the string doesn't start with three dotted numbers.
static bool parseSemver(const String& v, long out[3]) {
  out[0] = out[1] = out[2] = 0;
  int start = 0;
  for (int i = 0; i < 3; i++) {
    if (start >= (int)v.length() || !isDigit(v[start])) return false;
    long n = 0;
    while (start < (int)v.length() && isDigit(v[start])) {
      n = n * 10 + (v[start] - '0');
      start++;
    }
    out[i] = n;
    if (i < 2) {
      if (start >= (int)v.length() || v[start] != '.') return false;
      start++;  // skip '.'
    }
  }
  return true;
}

// True only when `remote` is a strictly newer semver than `local`. If either
// version can't be parsed (e.g. a "dev" / git-describe build), returns false so
// we never auto-flash an unversioned unit or downgrade it.
static bool isNewerVersion(const String& remote, const String& local) {
  long r[3], l[3];
  if (!parseSemver(remote, r) || !parseSemver(local, l)) return false;
  for (int i = 0; i < 3; i++) {
    if (r[i] != l[i]) return r[i] > l[i];
  }
  return false;
}

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

#if OTA_INCLUDE_PRERELEASES
  JsonObject release = doc[0].as<JsonObject>();
  if (release.isNull()) {
    Serial.println("OTA: no releases found");
    return;
  }
#else
  JsonObject release = doc.as<JsonObject>();
#endif

  // tag_name is e.g. "v1.0.1" — strip leading 'v'
  String tag = release["tag_name"].as<String>();
  if (tag.startsWith("v")) tag = tag.substring(1);

  if (!isNewerVersion(tag, FIRMWARE_VERSION)) {
    Serial.printf("OTA: up to date (running %s, latest %s)\n",
      FIRMWARE_VERSION, tag.c_str());
    return;
  }

  // Find firmware.bin asset download URL
  String downloadUrl;
  for (JsonObject asset : release["assets"].as<JsonArray>()) {
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
