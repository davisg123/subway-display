#ifndef OTA_H
#define OTA_H

#include "version.h"

// Set to true to make the main loop run an OTA check ASAP (e.g. from the admin
// "Check for updates" button), independent of the periodic interval.
extern volatile bool otaCheckRequested;

// Call once after WiFi connects, then periodically from loop().
// Whether pre-releases are considered is decided at runtime via
// isPrereleaseChannel() (persisted per device), so it survives OTA updates.
void checkForOTAUpdate();

#endif
