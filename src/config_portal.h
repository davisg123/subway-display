#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

#include <Arduino.h>

// Configuration structure
struct DeviceConfig {
  char wifi_ssid[33];
  char wifi_password[65];
  double latitude;
  double longitude;
  int train_limit;
  char station_ids[512];
};

// Initialize the config portal system (loads saved config from flash)
void initConfigPortal();

// Start WiFi connection - returns true if connected, false if AP mode started
// If AP mode starts, call handleConfigPortal() in your loop
bool startWiFi();

// Handle config portal requests (call in loop when in AP mode)
void handleConfigPortal();

// Check if device is in AP (config) mode
bool isInConfigMode();

// Get current configuration
DeviceConfig* getConfig();

// Start the web server for configuration (call after WiFi connected)
void startConfigServer();

// Get the configured API URL with lat/lon/limit
String getApiUrl();

#endif
