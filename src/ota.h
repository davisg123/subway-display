#ifndef OTA_H
#define OTA_H

#include "version.h"

#define OTA_INCLUDE_PRERELEASES 1

// Call once after WiFi connects, then periodically from loop()
void checkForOTAUpdate();

#endif
