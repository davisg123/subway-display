#ifndef DISPLAY_H
#define DISPLAY_H

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 2

// Set to true to run lamp test (cycles red, green, blue)
#define LAMP_TEST_ENABLED false

// Set to true to show Kalshi sports probabilities instead of subway times
#define KALSHI_MODE_ENABLED false

#define MAX_TRAINS 4

struct TrainArrival {
  char route;
  const char* title;
  int minutesAway;
};

void initDisplay();
void setTrainArrivals(TrainArrival* trains, int count);
void updateDisplay();  // Call from loop() to animate
void updateAPModeDisplay();  // Call from loop() when in AP mode
uint16_t getRouteColor(char route);
void runLampTest();

// Get display pointer (for other modules like kalshi_display)
MatrixPanel_I2S_DMA* getDisplay();

#endif
