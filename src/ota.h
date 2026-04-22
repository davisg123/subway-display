#ifndef OTA_H
#define OTA_H

#define FIRMWARE_VERSION "0.0.1"
#define OTA_INCLUDE_PRERELEASES 1

// Call once after WiFi connects, then periodically from loop()
void checkForOTAUpdate();

#endif
