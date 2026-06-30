#include "ota.h"
#include "display.h"
#include "config_portal.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#define OTA_REPO        "davisg123/subway-display"
// Pre-release channel lists all releases (newest first); the stable channel
// asks GitHub for the latest non-prerelease. Chosen at runtime per device.
#define OTA_API_URL_PRERELEASE "https://api.github.com/repos/" OTA_REPO "/releases"
#define OTA_API_URL_STABLE     "https://api.github.com/repos/" OTA_REPO "/releases/latest"

volatile bool otaCheckRequested = false;

// Parse "major.minor.patch" with an optional pre-release suffix (e.g.
// "-rc.2"). Fills core[3]; sets *preRank to 0 for a stable release (no suffix)
// or the trailing rc ordinal (rc.2 -> 2, >=1) for a pre-release. Returns false
// if the string doesn't start with three dotted numbers.
static bool parseSemver(const String& v, long core[3], long* preRank) {
  core[0] = core[1] = core[2] = 0;
  *preRank = 0;
  int start = 0;
  for (int i = 0; i < 3; i++) {
    if (start >= (int)v.length() || !isDigit(v[start])) return false;
    long n = 0;
    while (start < (int)v.length() && isDigit(v[start])) {
      n = n * 10 + (v[start] - '0');
      start++;
    }
    core[i] = n;
    if (i < 2) {
      if (start >= (int)v.length() || v[start] != '.') return false;
      start++;  // skip '.'
    }
  }
  // Anything after patch (e.g. "-rc.2") marks a pre-release. Use the last run
  // of digits as the ordinal so rc.2 outranks rc.1; default 1 if none.
  if (start < (int)v.length()) {
    long n = 0;
    bool sawDigit = false;
    for (int i = start; i < (int)v.length(); i++) {
      if (isDigit(v[i])) { n = n * 10 + (v[i] - '0'); sawDigit = true; }
      else { n = 0; sawDigit = false; }
    }
    *preRank = sawDigit ? n : 1;
  }
  return true;
}

// True only when `remote` is a strictly newer version than `local`. If either
// version can't be parsed (e.g. a "dev" / git-describe build), returns false so
// we never auto-flash an unversioned unit or downgrade it. Pre-release ordering
// follows semver: for the same major.minor.patch a stable build outranks any
// pre-release, and a higher rc ordinal outranks a lower one.
static bool isNewerVersion(const String& remote, const String& local) {
  long r[3], l[3], rPre, lPre;
  if (!parseSemver(remote, r, &rPre) || !parseSemver(local, l, &lPre)) return false;
  for (int i = 0; i < 3; i++) {
    if (r[i] != l[i]) return r[i] > l[i];
  }
  // Same core version: compare pre-release rank (0 == stable, ranks highest).
  bool rStable = (rPre == 0), lStable = (lPre == 0);
  if (rStable != lStable) return rStable;  // stable beats any pre-release
  if (rStable) return false;               // both stable -> equal
  return rPre > lPre;                      // both pre-release -> higher rc wins
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
  bool prerelease = isPrereleaseChannel();
  const char* apiUrl = prerelease ? OTA_API_URL_PRERELEASE : OTA_API_URL_STABLE;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, apiUrl);
  http.addHeader("User-Agent", "SubwaySign");
  http.setTimeout(8000);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("OTA check failed: HTTP %d\n", code);
    http.end();
    return;
  }

  // Only keep the few fields we need. GitHub's release JSON is large and grows
  // with every release (the /releases array especially), so filtering keeps the
  // on-device parse cheap and bounded regardless of how many releases exist.
  JsonDocument filter;
  if (prerelease) {
    // Array of releases: filter each element (newest first).
    JsonObject f = filter[0].to<JsonObject>();
    f["tag_name"] = true;
    f["assets"][0]["name"] = true;
    f["assets"][0]["browser_download_url"] = true;
  } else {
    // Single release object.
    filter["tag_name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(
    doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    Serial.printf("OTA JSON parse error: %s\n", err.c_str());
    return;
  }

  // The prerelease endpoint returns an array (newest first); the stable
  // endpoint returns a single release object.
  JsonObject release = prerelease ? doc[0].as<JsonObject>() : doc.as<JsonObject>();
  if (release.isNull()) {
    Serial.println("OTA: no releases found");
    return;
  }

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
  // GitHub release asset URLs 302-redirect to release-assets.githubusercontent.com;
  // HTTPUpdate disables redirects by default, so the download must opt in or it fails.
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
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
