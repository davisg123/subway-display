#ifndef OTA_H
#define OTA_H

#define FIRMWARE_VERSION "1.0.0"

// Call once after WiFi connects, then periodically from loop()
void checkForOTAUpdate();

#endif
