#ifndef DISPLAY_H
#define DISPLAY_H

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#define PANEL_RES_X 80
#define PANEL_RES_Y 40
#define PANEL_CHAIN 2

// Set to true to run lamp test (cycles red, green, blue)
#define LAMP_TEST_ENABLED false

// Set to true to show Kalshi sports probabilities instead of subway times
#define KALSHI_MODE_ENABLED false

// Set to true to use hardcoded demo coordinates when no saved config exists
#define IS_DEMO_MODE false
#define DEMO_LAT 40.706565
#define DEMO_LON -74.011333

#define MAX_TRAINS 4

struct TrainArrival {
  char route;
  const char* title;
  int minutesAway;
  int minutesAway2;  // second arrival on same route/direction, -1 if none
};

void initDisplay();
void setTrainArrivals(TrainArrival* trains, int count);
void updateDisplay();  // Call from loop() to animate
void setDisplayPower(bool on);  // Blank the panel when the schedule says "off"
void updateAPModeDisplay();  // Call from loop() when in AP mode
void showConnectedDisplay(const char* ipAddr);  // Show after WiFi connects, before trains load
uint16_t getRouteColor(char route);
void runLampTest();

// Get display pointer (for other modules like kalshi_display)
MatrixPanel_I2S_DMA* getDisplay();

#endif
