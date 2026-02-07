#include "display.h"
#include <cstring>

MatrixPanel_I2S_DMA* dma_display = nullptr;

const int DISPLAY_WIDTH = PANEL_RES_X * PANEL_CHAIN;  // 128
const int DISPLAY_HEIGHT = PANEL_RES_Y;               // 32
const int ROW_HEIGHT = 16;

const int CIRCLE_RADIUS = 6;
const int CIRCLE_X = 8;
const int MINUTES_WIDTH = 42;  // "XX min" right-justified

// Animation state
TrainArrival storedArrivals[MAX_TRAINS];
int storedArrivalCount = 0;
int currentRow2Index = 1;        // Which train (1-3) is currently shown on row 2
int animationOffset = 0;         // Current Y offset for wipe animation (0 to ROW_HEIGHT)
bool isAnimating = false;
unsigned long lastAnimationTime = 0;
unsigned long lastTrainSwitch = 0;

const unsigned long DISPLAY_DURATION_MS = 10000;  // Show each train for 10 seconds
const unsigned long ANIMATION_STEP_MS = 62;       // ~16fps during animation
const int ANIMATION_SPEED = 1;                    // Pixels per step (16 steps * 62ms ≈ 1 second)

void initDisplay() {
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);

  // MatrixPortal ESP32-S3 specific pin configuration
  mxconfig.gpio.r1 = 42;
  mxconfig.gpio.g1 = 41;
  mxconfig.gpio.b1 = 40;
  mxconfig.gpio.r2 = 38;
  mxconfig.gpio.g2 = 39;
  mxconfig.gpio.b2 = 37;
  mxconfig.gpio.a = 45;
  mxconfig.gpio.b = 36;
  mxconfig.gpio.c = 48;
  mxconfig.gpio.d = 35;
  mxconfig.gpio.e = 21;
  mxconfig.gpio.lat = 47;
  mxconfig.gpio.oe = 14;
  mxconfig.gpio.clk = 2;

  mxconfig.driver = HUB75_I2S_CFG::ICN2038S;
  mxconfig.clkphase = false;
  mxconfig.latch_blanking = 1;
  mxconfig.double_buff = true;  // Enable double buffering for flicker-free animation

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(90);
  dma_display->clearScreen();
  dma_display->flipDMABuffer();
  dma_display->clearScreen();
}

uint16_t getRouteColor(char route) {
  switch (route) {
    // Red: 1, 2, 3
    case '1': case '2': case '3':
      return dma_display->color565(255, 0, 0);
    // Green: 4, 5, 6
    case '4': case '5': case '6':
      return dma_display->color565(0, 147, 60);
    // Purple: 7
    case '7':
      return dma_display->color565(185, 51, 173);
    // Blue: A, C, E
    case 'A': case 'C': case 'E':
      return dma_display->color565(0, 57, 166);
    // Orange: B, D, F, M
    case 'B': case 'D': case 'F': case 'M':
      return dma_display->color565(255, 99, 25);
    // Lime: G
    case 'G':
      return dma_display->color565(108, 190, 69);
    // Brown: J, Z
    case 'J': case 'Z':
      return dma_display->color565(153, 102, 51);
    // Yellow: N, Q, R, W
    case 'N': case 'Q': case 'R': case 'W':
      return dma_display->color565(252, 204, 10);
    // Gray: L, S
    case 'L': case 'S':
      return dma_display->color565(167, 169, 172);
    default:
      return dma_display->color565(255, 255, 255);
  }
}

void drawRouteCircle(int centerX, int centerY, char route) {
  uint16_t color = getRouteColor(route);
  dma_display->fillCircle(centerX, centerY, CIRCLE_RADIUS, color);

  // Draw letter in white (or black for yellow lines)
  uint16_t textColor = dma_display->color565(255, 255, 255);
  if (route == 'N' || route == 'Q' || route == 'R' || route == 'W') {
    textColor = dma_display->color565(0, 0, 0);
  }

  dma_display->setTextSize(1);
  dma_display->setTextColor(textColor);
  // Default font is 5x7 pixels, cursor is top-left
  // Center: X - 2 (5/2), Y - 3 (7/2)
  dma_display->setCursor(centerX - 2, centerY - 3);
  dma_display->print(route);
}

void drawTrainRow(int yOffset, TrainArrival arrival) {
  int centerY = yOffset + ROW_HEIGHT / 2;

  // Draw route circle
  drawRouteCircle(CIRCLE_X, centerY, arrival.route);

  // Calculate available space for title
  int titleStartX = CIRCLE_X + CIRCLE_RADIUS + 4;
  int minutesStartX = DISPLAY_WIDTH - MINUTES_WIDTH;
  int titleMaxWidth = minutesStartX - titleStartX - 2;

  // Draw title (truncated if needed)
  dma_display->setTextSize(1);
  dma_display->setTextColor(dma_display->color565(255, 255, 255));
  dma_display->setCursor(titleStartX, centerY - 4);

  int charWidth = 6;
  int maxChars = titleMaxWidth / charWidth;
  int titleLen = strlen(arrival.title);

  if (titleLen <= maxChars) {
    dma_display->print(arrival.title);
  } else {
    for (int i = 0; i < maxChars; i++) {
      dma_display->print(arrival.title[i]);
    }
  }

  // Draw minutes right-justified
  char minStr[10];
  snprintf(minStr, sizeof(minStr), "%d min", arrival.minutesAway);
  int minStrLen = strlen(minStr);
  int minX = DISPLAY_WIDTH - (minStrLen * charWidth) - 2;

  dma_display->setCursor(minX, centerY - 4);
  dma_display->print(minStr);
}

// Draw a train row at the given Y offset (no clipping - draws full row)
void drawTrainRowAt(int yOffset, TrainArrival arrival) {
  int centerY = yOffset + ROW_HEIGHT / 2;

  // Draw route circle
  drawRouteCircle(CIRCLE_X, centerY, arrival.route);

  // Calculate available space for title
  int titleStartX = CIRCLE_X + CIRCLE_RADIUS + 4;
  int minutesStartX = DISPLAY_WIDTH - MINUTES_WIDTH;
  int titleMaxWidth = minutesStartX - titleStartX - 2;

  // Draw title
  int textY = centerY - 4;
  dma_display->setTextSize(1);
  dma_display->setTextColor(dma_display->color565(255, 255, 255));
  dma_display->setCursor(titleStartX, textY);

  int charWidth = 6;
  int maxChars = titleMaxWidth / charWidth;
  int titleLen = strlen(arrival.title);

  if (titleLen <= maxChars) {
    dma_display->print(arrival.title);
  } else {
    for (int i = 0; i < maxChars; i++) {
      dma_display->print(arrival.title[i]);
    }
  }

  // Draw minutes right-justified
  char minStr[10];
  snprintf(minStr, sizeof(minStr), "%d min", arrival.minutesAway);
  int minStrLen = strlen(minStr);
  int minX = DISPLAY_WIDTH - (minStrLen * charWidth) - 2;

  dma_display->setCursor(minX, textY);
  dma_display->print(minStr);
}

void setTrainArrivals(TrainArrival* trains, int count) {
  storedArrivalCount = count > MAX_TRAINS ? MAX_TRAINS : count;
  for (int i = 0; i < storedArrivalCount; i++) {
    storedArrivals[i] = trains[i];
  }

  // Reset animation state
  currentRow2Index = 1;
  animationOffset = 0;
  isAnimating = false;
  lastTrainSwitch = millis();

  // Draw initial state
  dma_display->clearScreen();
  if (storedArrivalCount >= 1) {
    drawTrainRow(0, storedArrivals[0]);
  }
  if (storedArrivalCount >= 2) {
    drawTrainRow(ROW_HEIGHT, storedArrivals[1]);
  }
  dma_display->flipDMABuffer();
}

void updateDisplay() {
  // Only animate if we have more than 2 trains
  if (storedArrivalCount <= 2) return;

  unsigned long now = millis();

  // Check if it's time to start animating to next train
  if (!isAnimating && (now - lastTrainSwitch >= DISPLAY_DURATION_MS)) {
    isAnimating = true;
    animationOffset = 0;
    lastAnimationTime = now;
  }

  // Perform animation step
  if (isAnimating && (now - lastAnimationTime >= ANIMATION_STEP_MS)) {
    lastAnimationTime = now;
    animationOffset += ANIMATION_SPEED;

    // Calculate next train index (cycle through trains 1, 2, 3 for row 2)
    int nextIndex = currentRow2Index + 1;
    if (nextIndex >= storedArrivalCount) {
      nextIndex = 1;  // Wrap back to train index 1
    }

    // Clear entire screen and redraw everything to back buffer
    dma_display->clearScreen();

    // Draw row 1 (static)
    drawTrainRow(0, storedArrivals[0]);

    // Draw current train sliding up (out)
    int currentY = ROW_HEIGHT - animationOffset;
    drawTrainRowAt(currentY, storedArrivals[currentRow2Index]);

    // Draw next train sliding up (in)
    int nextY = ROW_HEIGHT + ROW_HEIGHT - animationOffset;
    drawTrainRowAt(nextY, storedArrivals[nextIndex]);

    // Clip by painting black over areas outside the bottom row region
    dma_display->fillRect(0, 0, DISPLAY_WIDTH, ROW_HEIGHT, 0);  // Clear top overflow
    dma_display->fillRect(0, DISPLAY_HEIGHT, DISPLAY_WIDTH, ROW_HEIGHT, 0);  // Clear bottom overflow

    // Redraw row 1 since we cleared it
    drawTrainRow(0, storedArrivals[0]);

    // Flip buffer to display the completed frame
    dma_display->flipDMABuffer();

    // Check if animation is complete
    if (animationOffset >= ROW_HEIGHT) {
      isAnimating = false;
      animationOffset = 0;
      currentRow2Index = nextIndex;
      lastTrainSwitch = now;
    }
  }
}

void runLampTest() {
  int colorIndex = 0;
  while (true) {
    dma_display->fillScreenRGB888(
      colorIndex == 0 ? 255 : 0,  // R
      colorIndex == 1 ? 255 : 0,  // G
      colorIndex == 2 ? 255 : 0   // B
    );
    delay(1000);
    colorIndex = (colorIndex + 1) % 3;
  }
}

MatrixPanel_I2S_DMA* getDisplay() {
  return dma_display;
}
