#include "display.h"
#include <cmath>
#include <cstring>

MatrixPanel_I2S_DMA* dma_display = nullptr;

const int DISPLAY_WIDTH = PANEL_RES_X * PANEL_CHAIN;  // 80 * 2 = 160
const int DISPLAY_HEIGHT = PANEL_RES_Y;               // 40
const int ROW_HEIGHT = 20;  // 2 rows * 20 = 40 = full panel height, so rows are vertically centered

const int CIRCLE_RADIUS = 7;
const int CIRCLE_X = 10;  // keeps the circle clear of the left edge
const int MINUTES_WIDTH = 40;  // covers "XX min" with fixed label position

// Animation state
TrainArrival storedArrivals[MAX_TRAINS];
int storedArrivalCount = 0;
int currentRow2Index = 1;        // Which train (1-3) is currently shown on row 2
int animationOffset = 0;         // Current Y offset for wipe animation (0 to ROW_HEIGHT)
bool isAnimating = false;
unsigned long lastAnimationTime = 0;
unsigned long lastTrainSwitch = 0;
static bool displayOn = true;    // false when the schedule/override powers the sign off
static uint8_t currentBrightness = 90;  // 0-255; overridden by saved config at boot

const unsigned long DISPLAY_DURATION_MS = 15000;  // dwell before scrolling to next train
const unsigned long ANIMATION_STEP_MS = 100;  // ~10fps — 16 steps * 100ms ≈ 1.6s wipe
const int ANIMATION_SPEED = 1;

// Pulse state for dual-time arrivals
static float pulsePhase = 0.0f;
static unsigned long lastPulseUpdate = 0;
static const float PULSE_SPEED = 0.25f;        // cycles/sec — drives redraw cadence
static const unsigned long PULSE_STEP_MS = 50; // 20fps pulse update
static const unsigned long PULSE_FADE_MS = 400;   // crossfade in/out duration
static const unsigned long PULSE_HOLD_MS = 5000;  // hold each time steady for 5s

void initDisplay() {
  Serial.println("initDisplay: start");

  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);

  // MatrixPortal ESP32-S3 specific pin configuration
  // mxconfig.gpio.r1 = 42;
  // mxconfig.gpio.g1 = 41;
  // mxconfig.gpio.b1 = 40;
  // mxconfig.gpio.r2 = 38;
  // mxconfig.gpio.g2 = 39;
  // mxconfig.gpio.b2 = 37;
  // mxconfig.gpio.a = 45;
  // mxconfig.gpio.b = 36;
  // mxconfig.gpio.c = 48;
  // mxconfig.gpio.d = 35;
  // mxconfig.gpio.e = 21;
  // mxconfig.gpio.lat = 47;
  // mxconfig.gpio.oe = 14;
  // mxconfig.gpio.clk = 2;

  // Huidu WF2 ESP32-S3 pin configuration (75EX1 connector)
  mxconfig.gpio.r1  = 2;
  mxconfig.gpio.r2  = 3;
  mxconfig.gpio.g1  = 6;
  mxconfig.gpio.g2  = 7;
  mxconfig.gpio.b1  = 10;
  mxconfig.gpio.b2  = 11;
  mxconfig.gpio.a   = 39;
  mxconfig.gpio.b   = 38;
  mxconfig.gpio.c   = 37;
  mxconfig.gpio.d   = 36;
  mxconfig.gpio.e   = 21;
  mxconfig.gpio.lat = 33;
  mxconfig.gpio.oe  = 35;
  mxconfig.gpio.clk = 34;

  // Use SHIFTREG (generic) driver — avoids ICN2038S init sequence corrupting panels
  // that don't need it. Switch back to ICN2038S if colors are wrong.
  mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
  mxconfig.clkphase = false;
  mxconfig.latch_blanking = 1;
  mxconfig.double_buff = true;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  bool ok = dma_display->begin();
  Serial.printf("initDisplay: begin() returned %s\n", ok ? "true" : "false");

  dma_display->setBrightness8(currentBrightness);
  dma_display->clearScreen();
  dma_display->flipDMABuffer();
  dma_display->clearScreen();

  Serial.println("initDisplay: done");
}

// Set panel brightness (0-255). Persists across power on/off since the value
// lives in the driver's refresh; reapplied here so dashboard changes are live.
void setDisplayBrightness(uint8_t value) {
  currentBrightness = value;
  if (dma_display) dma_display->setBrightness8(value);
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

  // Widen each cardinal point into a flat 3px cap (tangent to the edge) so the
  // lone 1px tip reads as a flattened side instead of a spike.
  const int r = CIRCLE_RADIUS;
  for (int i = -1; i <= 1; i++) {
    dma_display->drawPixel(centerX + i, centerY - r, color);  // top cap
    dma_display->drawPixel(centerX + i, centerY + r, color);  // bottom cap
    dma_display->drawPixel(centerX - r, centerY + i, color);  // left cap
    dma_display->drawPixel(centerX + r, centerY + i, color);  // right cap
  }

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

// " min" is always drawn at this fixed x so the label never shifts
static const int MIN_LABEL_X = DISPLAY_WIDTH - 4 * 6 - 2;  // 4 chars: " min"

// A second arrival is only worth pulsing if it exists and differs from the first.
static inline bool hasSecondTime(const TrainArrival& a) {
  return a.minutesAway2 != -1 && a.minutesAway2 != a.minutesAway;
}

static void drawMinutes(int y, const TrainArrival& arrival) {
  const int charWidth = 6;

  if (hasSecondTime(arrival)) {
    // Show one number at a time: fade in, hold steady, fade out, then the other.
    const unsigned long half = PULSE_FADE_MS + PULSE_HOLD_MS + PULSE_FADE_MS;
    const unsigned long cycle = 2 * half;
    unsigned long t = millis() % cycle;
    bool showFirst = t < half;
    unsigned long u = showFirst ? t : t - half;  // position within this time's window

    float brightness;
    if (u < PULSE_FADE_MS)
      brightness = (float)u / PULSE_FADE_MS;                                       // fade in
    else if (u < PULSE_FADE_MS + PULSE_HOLD_MS)
      brightness = 1.0f;                                                            // hold
    else
      brightness = 1.0f - (float)(u - PULSE_FADE_MS - PULSE_HOLD_MS) / PULSE_FADE_MS; // fade out

    int minutes = showFirst ? arrival.minutesAway : arrival.minutesAway2;
    int v = (int)(brightness * 255);

    char numStr[4];
    int numLen = snprintf(numStr, sizeof(numStr), "%d", minutes);
    int numX = MIN_LABEL_X - numLen * charWidth;

    dma_display->setTextColor(dma_display->color565(v, v, v));
    dma_display->setCursor(numX, y);
    dma_display->print(numStr);

    // " min" label always at full brightness, fixed position
    dma_display->setTextColor(dma_display->color565(255, 255, 255));
    dma_display->setCursor(MIN_LABEL_X, y);
    dma_display->print(" min");
  } else {
    char minStr[10];
    snprintf(minStr, sizeof(minStr), "%d min", arrival.minutesAway);
    int minX = DISPLAY_WIDTH - (int)strlen(minStr) * charWidth - 2;
    dma_display->setTextColor(dma_display->color565(255, 255, 255));
    dma_display->setCursor(minX, y);
    dma_display->print(minStr);
  }
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

  drawMinutes(centerY - 4, arrival);
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

  drawMinutes(textY, arrival);
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

  // Keep the panel dark if powered off; data is stored for when it turns back on.
  if (!displayOn) return;

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

// Power the panel on/off without losing the stored arrivals.
void setDisplayPower(bool on) {
  if (on == displayOn) return;
  displayOn = on;
  if (!on) {
    // Clear both DMA buffers so nothing remains lit.
    dma_display->clearScreen();
    dma_display->flipDMABuffer();
    dma_display->clearScreen();
    dma_display->flipDMABuffer();
  } else {
    // Redraw the current arrivals immediately.
    currentRow2Index = 1;
    animationOffset = 0;
    isAnimating = false;
    lastTrainSwitch = millis();
    dma_display->clearScreen();
    if (storedArrivalCount >= 1) drawTrainRow(0, storedArrivals[0]);
    if (storedArrivalCount >= 2) drawTrainRow(ROW_HEIGHT, storedArrivals[1]);
    dma_display->flipDMABuffer();
  }
}

void updateDisplay() {
  if (!displayOn) return;
  unsigned long now = millis();

  // Advance pulse phase whenever any arrival has two times
  bool pulseAdvanced = false;
  if (now - lastPulseUpdate >= PULSE_STEP_MS) {
    for (int i = 0; i < storedArrivalCount; i++) {
      if (hasSecondTime(storedArrivals[i])) {
        float dt = (now - lastPulseUpdate) / 1000.0f;
        pulsePhase = fmodf(pulsePhase + dt * PULSE_SPEED, 1.0f);
        lastPulseUpdate = now;
        pulseAdvanced = true;
        break;
      }
    }
  }

  if (storedArrivalCount <= 2) {
    if (pulseAdvanced) {
      dma_display->clearScreen();
      if (storedArrivalCount >= 1) drawTrainRow(0, storedArrivals[0]);
      if (storedArrivalCount >= 2) drawTrainRow(ROW_HEIGHT, storedArrivals[1]);
      dma_display->flipDMABuffer();
    }
    return;
  }

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
  } else if (!isAnimating && pulseAdvanced) {
    dma_display->clearScreen();
    drawTrainRow(0, storedArrivals[0]);
    drawTrainRow(ROW_HEIGHT, storedArrivals[currentRow2Index]);
    dma_display->flipDMABuffer();
  }
}

static unsigned long lastAPScrollTime = 0;
void updateAPModeDisplay() {
  unsigned long now = millis();
  if (now - lastAPScrollTime < 500) return;
  lastAPScrollTime = now;

  dma_display->clearScreen();

  dma_display->setTextSize(1);
  dma_display->setTextColor(dma_display->color565(255, 200, 0));
  dma_display->setCursor(2, 4);
  dma_display->print("WiFi: Sign Setup");

  dma_display->setTextColor(dma_display->color565(0, 255, 255));
  dma_display->setCursor(2, 20);
  dma_display->print("192.168.4.1");

  dma_display->flipDMABuffer();
}

void showConnectedDisplay(const char* ipAddr) {
  dma_display->clearScreen();

  dma_display->setTextSize(1);
  dma_display->setTextColor(dma_display->color565(0, 255, 100));
  dma_display->setCursor(2, 4);
  dma_display->print("WiFi connected!");

  dma_display->setTextColor(dma_display->color565(0, 255, 255));
  dma_display->setCursor(2, 13);
  dma_display->print("Visit sign.local");

  dma_display->setTextColor(dma_display->color565(150, 150, 150));
  dma_display->setCursor(2, 23);
  dma_display->print(ipAddr);

  dma_display->flipDMABuffer();
}

void runLampTest() {
  const char* colors[] = {"RED", "GREEN", "BLUE"};
  int colorIndex = 0;
  while (true) {
    Serial.printf("runLampTest: %s\n", colors[colorIndex]);
    dma_display->fillScreenRGB888(
      colorIndex == 0 ? 255 : 0,
      colorIndex == 1 ? 255 : 0,
      colorIndex == 2 ? 255 : 0
    );
    dma_display->flipDMABuffer();
    delay(1000);
    colorIndex = (colorIndex + 1) % 3;
  }
}

MatrixPanel_I2S_DMA* getDisplay() {
  return dma_display;
}
