#include "kalshi_display.h"
#include <cstring>
#include <cmath>

// Display reference (passed from main display init)
static MatrixPanel_I2S_DMA* dma_display = nullptr;

// Display constants
static const int DISPLAY_WIDTH = 128;
static const int DISPLAY_HEIGHT = 32;
static const int ROW_HEIGHT = 16;

// Stored matchup data
static SportsMatchup storedMatchup;

// Animation state
static int animatedProb1 = 0;
static int targetProb1 = 0;
static unsigned long lastAnimTime = 0;
static const unsigned long ANIM_STEP_MS = 16;  // ~60fps for smooth wave animation

// Pulse/glow animation state
static unsigned long pulseStartTime = 0;
static const unsigned long PULSE_PERIOD_MS = 2000;  // Full pulse cycle duration (slower)
static const float PULSE_MIN_BRIGHTNESS = 0.85f;    // Subtle dimming
static const float PULSE_MAX_BRIGHTNESS = 1.4f;     // Gentle peak

// Utility: brighten a RGB565 color by a factor
static uint16_t brightenColor(uint16_t color, float factor) {
  // Extract RGB components from RGB565
  uint8_t r = ((color >> 11) & 0x1F) << 3;  // 5 bits -> 8 bits
  uint8_t g = ((color >> 5) & 0x3F) << 2;   // 6 bits -> 8 bits
  uint8_t b = (color & 0x1F) << 3;          // 5 bits -> 8 bits

  // Brighten (clamp to 255)
  r = min(255, (int)(r * factor));
  g = min(255, (int)(g * factor));
  b = min(255, (int)(b * factor));

  return dma_display->color565(r, g, b);
}

// Get current pulse brightness factor (1.0 to PULSE_MAX_BRIGHTNESS)
static float getPulseBrightness() {
  unsigned long elapsed = millis() - pulseStartTime;
  float phase = (float)(elapsed % PULSE_PERIOD_MS) / PULSE_PERIOD_MS;
  // Sine wave oscillation between min and max brightness
  float brightness = PULSE_MIN_BRIGHTNESS +
    (PULSE_MAX_BRIGHTNESS - PULSE_MIN_BRIGHTNESS) * (0.5f + 0.5f * sin(phase * 2.0f * M_PI));
  return brightness;
}

// ============== Basic Effect ==============
#if KALSHI_EFFECT == KALSHI_EFFECT_BASIC

static const int PROB_BAR_START_X = 70;
static const int PROB_BAR_WIDTH = 45;
static const int PROB_BAR_HEIGHT = 10;
static const int BADGE_RADIUS = 5;
static const int BADGE_X = 6;

static void drawTeamBadge(int centerX, int centerY, uint16_t color) {
  dma_display->fillCircle(centerX, centerY, BADGE_RADIUS, color);
}

static void drawProbabilityBar(int x, int y, int width, int height, int probability, uint16_t fillColor) {
  uint16_t bgColor = dma_display->color565(40, 40, 40);
  dma_display->fillRect(x, y, width, height, bgColor);

  int fillWidth = (width * probability) / 100;
  if (fillWidth > 0) {
    dma_display->fillRect(x, y, fillWidth, height, fillColor);
  }

  uint16_t borderColor = dma_display->color565(80, 80, 80);
  dma_display->drawRect(x, y, width, height, borderColor);
}

static void drawTeamRow(int yOffset, SportsTeam team, int animatedProb) {
  int centerY = yOffset + ROW_HEIGHT / 2;

  drawTeamBadge(BADGE_X, centerY, team.color);

  int nameStartX = BADGE_X + BADGE_RADIUS + 4;
  dma_display->setTextSize(1);
  dma_display->setTextColor(dma_display->color565(255, 255, 255));
  dma_display->setCursor(nameStartX, centerY - 3);

  char nameBuf[9];
  strncpy(nameBuf, team.name, 8);
  nameBuf[8] = '\0';
  dma_display->print(nameBuf);

  int barY = centerY - PROB_BAR_HEIGHT / 2;
  drawProbabilityBar(PROB_BAR_START_X, barY, PROB_BAR_WIDTH, PROB_BAR_HEIGHT, animatedProb, team.color);

  char probStr[6];
  snprintf(probStr, sizeof(probStr), "%d%%", animatedProb);
  int probStrLen = strlen(probStr);
  int charWidth = 6;
  int probX = DISPLAY_WIDTH - (probStrLen * charWidth) - 2;
  dma_display->setCursor(probX, centerY - 3);
  dma_display->print(probStr);
}

static void drawBasicEffect() {
  dma_display->clearScreen();
  drawTeamRow(0, storedMatchup.team1, animatedProb1);
  drawTeamRow(ROW_HEIGHT, storedMatchup.team2, 100 - animatedProb1);
  dma_display->flipDMABuffer();
}

#endif // KALSHI_EFFECT_BASIC

// ============== Tug-of-War Effect ==============
#if KALSHI_EFFECT == KALSHI_EFFECT_TUG_OF_WAR

static const int TUG_BAR_Y = 12;       // Vertical center of the tug bar
static const int TUG_BAR_HEIGHT = 8;   // Height of the main bar
static const int TUG_BAR_MARGIN = 4;   // Margin from screen edges
static const int TUG_BAR_WIDTH = DISPLAY_WIDTH - (TUG_BAR_MARGIN * 2);

// Rope knot position (the dividing line)
static float knotPosition = 0.5f;  // 0.0 = full left, 1.0 = full right
static float targetKnotPosition = 0.5f;
static const float KNOT_SPEED = 0.02f;  // How fast the knot moves

static void drawTugOfWarBar() {
  dma_display->clearScreen();

  int barX = TUG_BAR_MARGIN;
  int barY = TUG_BAR_Y;

  // Calculate knot pixel position
  int knotX = barX + (int)(knotPosition * TUG_BAR_WIDTH);

  // Determine winner and get pulse brightness
  bool team1Winning = (animatedProb1 > 50);
  bool team2Winning = (animatedProb1 < 50);
  float pulseBrightness = getPulseBrightness();

  // Get colors (only pulse the bar, not the text)
  uint16_t team1BarColor = storedMatchup.team1.color;
  uint16_t team2BarColor = storedMatchup.team2.color;

  if (team1Winning) {
    team1BarColor = brightenColor(storedMatchup.team1.color, pulseBrightness);
  } else if (team2Winning) {
    team2BarColor = brightenColor(storedMatchup.team2.color, pulseBrightness);
  }

  // Draw team 1 side (left)
  int team1Width = knotX - barX;
  if (team1Width > 0) {
    dma_display->fillRect(barX, barY, team1Width, TUG_BAR_HEIGHT, team1BarColor);
  }

  // Draw team 2 side (right)
  int team2Width = (barX + TUG_BAR_WIDTH) - knotX;
  if (team2Width > 0) {
    dma_display->fillRect(knotX, barY, team2Width, TUG_BAR_HEIGHT, team2BarColor);
  }

  // Draw the "knot" (center divider) - a small white marker
  uint16_t knotColor = dma_display->color565(255, 255, 255);
  dma_display->drawFastVLine(knotX, barY - 2, TUG_BAR_HEIGHT + 4, knotColor);

  // Draw team names and percentages
  dma_display->setTextSize(1);
  int charWidth = 6;

  // Team 1 (left side) - name on top, percentage below bar
  dma_display->setTextColor(storedMatchup.team1.color);
  dma_display->setCursor(TUG_BAR_MARGIN, 2);
  char name1[9];
  strncpy(name1, storedMatchup.team1.name, 8);
  name1[8] = '\0';
  dma_display->print(name1);

  // Team 1 percentage (below bar, left side)
  char prob1Str[5];
  snprintf(prob1Str, sizeof(prob1Str), "%d%%", animatedProb1);
  dma_display->setCursor(TUG_BAR_MARGIN, TUG_BAR_Y + TUG_BAR_HEIGHT + 4);
  dma_display->print(prob1Str);

  // Team 2 (right side) - name on top right, percentage below bar right
  dma_display->setTextColor(storedMatchup.team2.color);
  int name2Len = strlen(storedMatchup.team2.name);
  if (name2Len > 8) name2Len = 8;
  int name2X = DISPLAY_WIDTH - TUG_BAR_MARGIN - (name2Len * charWidth);
  dma_display->setCursor(name2X, 2);
  char name2[9];
  strncpy(name2, storedMatchup.team2.name, 8);
  name2[8] = '\0';
  dma_display->print(name2);

  // Team 2 percentage
  char prob2Str[5];
  int prob2 = 100 - animatedProb1;
  snprintf(prob2Str, sizeof(prob2Str), "%d%%", prob2);
  int prob2Len = strlen(prob2Str);
  int prob2X = DISPLAY_WIDTH - TUG_BAR_MARGIN - (prob2Len * charWidth);
  dma_display->setCursor(prob2X, TUG_BAR_Y + TUG_BAR_HEIGHT + 4);
  dma_display->print(prob2Str);

  dma_display->flipDMABuffer();
}

#endif // KALSHI_EFFECT_TUG_OF_WAR

// ============== Rising Tide Effect ==============
#if KALSHI_EFFECT == KALSHI_EFFECT_RISING_TIDE

static float wavePhase1 = 0.0f;  // Phase for team 1 waves (animates over time)
static float wavePhase2 = 0.0f;  // Phase for team 2 waves
static const float WAVE_SPEED_1 = 0.06f;   // Team 1 wave animation speed (slower)
static const float WAVE_SPEED_2 = -0.05f;  // Team 2 waves move opposite direction (slower)
static const int WAVE_AMPLITUDE = 4;       // Wave height variation in pixels (larger)
static const int BASE_WAVE_HEIGHT = 10;    // Base height of the wave field
static const int WAVE_TOP_LIMIT = 18;      // Waves can't go above this y position
static const int TEXT_MARGIN = 2;

// Get wave height at a given x position for team 1 (left side)
static int getWaveHeight1(int x, float phase) {
  float wave = sin((x * 0.15f) + phase) + sin((x * 0.08f) + phase * 0.7f) * 0.5f;
  return (int)(wave * WAVE_AMPLITUDE);
}

// Get wave height at a given x position for team 2 (right side)
static int getWaveHeight2(int x, float phase) {
  float wave = sin((x * 0.12f) + phase) + sin((x * 0.2f) + phase * 1.3f) * 0.5f;
  return (int)(wave * WAVE_AMPLITUDE);
}

static void drawRisingTide() {
  dma_display->clearScreen();

  // Calculate the meeting point based on probability
  int meetingX = (animatedProb1 * DISPLAY_WIDTH) / 100;

  // Determine winner and get pulse brightness
  bool team1Winning = (animatedProb1 > 50);
  bool team2Winning = (animatedProb1 < 50);
  float pulseBrightness = getPulseBrightness();

  // Draw waves for each column
  for (int x = 0; x < DISPLAY_WIDTH; x++) {
    int waveTop;
    uint16_t color;

    if (x < meetingX) {
      // Team 1 territory (left side)
      waveTop = DISPLAY_HEIGHT - BASE_WAVE_HEIGHT + getWaveHeight1(x, wavePhase1);
      color = storedMatchup.team1.color;

      // Apply pulse if team 1 is winning
      if (team1Winning) {
        color = brightenColor(color, pulseBrightness);
      }

      // Extra brighten waves near the meeting point (battle zone)
      if (x > meetingX - 10) {
        float intensity = 1.0f + (1.0f - (float)(meetingX - x) / 10.0f) * 0.3f;
        color = brightenColor(color, intensity);
      }
    } else {
      // Team 2 territory (right side)
      waveTop = DISPLAY_HEIGHT - BASE_WAVE_HEIGHT + getWaveHeight2(x, wavePhase2);
      color = storedMatchup.team2.color;

      // Apply pulse if team 2 is winning
      if (team2Winning) {
        color = brightenColor(color, pulseBrightness);
      }

      // Extra brighten waves near the meeting point (battle zone)
      if (x < meetingX + 10) {
        float intensity = 1.0f + (1.0f - (float)(x - meetingX) / 10.0f) * 0.3f;
        color = brightenColor(color, intensity);
      }
    }

    // Clamp wave top to valid range (keep below text area)
    if (waveTop < WAVE_TOP_LIMIT) waveTop = WAVE_TOP_LIMIT;
    if (waveTop > DISPLAY_HEIGHT - 1) waveTop = DISPLAY_HEIGHT - 1;

    // Draw the wave column (from waveTop to bottom)
    for (int y = waveTop; y < DISPLAY_HEIGHT; y++) {
      // Add slight color variation for depth
      uint16_t pixelColor = color;
      if (y == waveTop) {
        // Wave crest - slightly brighter
        pixelColor = brightenColor(color, 1.3f);
      }
      dma_display->drawPixel(x, y, pixelColor);
    }
  }

  // Draw team names at top
  dma_display->setTextSize(1);
  int charWidth = 6;

  // Team 1 name (top left)
  dma_display->setTextColor(dma_display->color565(255, 255, 255));
  dma_display->setCursor(TEXT_MARGIN, 1);
  char name1[11];
  strncpy(name1, storedMatchup.team1.name, 10);
  name1[10] = '\0';
  dma_display->print(name1);

  // Team 2 name (top right)
  int name2Len = strlen(storedMatchup.team2.name);
  if (name2Len > 10) name2Len = 10;
  int name2X = DISPLAY_WIDTH - TEXT_MARGIN - (name2Len * charWidth);
  dma_display->setCursor(name2X, 1);
  char name2[11];
  strncpy(name2, storedMatchup.team2.name, 10);
  name2[10] = '\0';
  dma_display->print(name2);

  // Draw percentages (centered under team names, slightly larger visual weight)
  char prob1Str[5];
  snprintf(prob1Str, sizeof(prob1Str), "%d%%", animatedProb1);
  dma_display->setCursor(TEXT_MARGIN, 10);
  dma_display->setTextColor(storedMatchup.team1.color);
  dma_display->print(prob1Str);

  char prob2Str[5];
  int prob2 = 100 - animatedProb1;
  snprintf(prob2Str, sizeof(prob2Str), "%d%%", prob2);
  int prob2Len = strlen(prob2Str);
  int prob2X = DISPLAY_WIDTH - TEXT_MARGIN - (prob2Len * charWidth);
  dma_display->setCursor(prob2X, 10);
  dma_display->setTextColor(storedMatchup.team2.color);
  dma_display->print(prob2Str);

  dma_display->flipDMABuffer();
}

#endif // KALSHI_EFFECT_RISING_TIDE

// ============== Public Interface ==============

void initKalshiDisplay(MatrixPanel_I2S_DMA* display) {
  dma_display = display;
}

void setSportsMatchup(SportsMatchup* matchup) {
  storedMatchup = *matchup;
  targetProb1 = matchup->team1.probability;

  // Initialize to target for first draw
  animatedProb1 = targetProb1;

  // Initialize pulse timer
  pulseStartTime = millis();

#if KALSHI_EFFECT == KALSHI_EFFECT_TUG_OF_WAR
  // Knot position: team1 probability as fraction (0-1)
  // At 50%, knot is centered. At 100% team1, knot is at right edge.
  targetKnotPosition = (float)targetProb1 / 100.0f;
  knotPosition = targetKnotPosition;
#endif

  // Initial draw
#if KALSHI_EFFECT == KALSHI_EFFECT_BASIC
  drawBasicEffect();
#elif KALSHI_EFFECT == KALSHI_EFFECT_TUG_OF_WAR
  drawTugOfWarBar();
#elif KALSHI_EFFECT == KALSHI_EFFECT_RISING_TIDE
  drawRisingTide();
#endif
}

void setTargetProbability(int team1Probability) {
  targetProb1 = team1Probability;

#if KALSHI_EFFECT == KALSHI_EFFECT_TUG_OF_WAR
  targetKnotPosition = (float)team1Probability / 100.0f;
#endif
}

void updateSportsDisplay() {
  unsigned long now = millis();

  if (now - lastAnimTime < ANIM_STEP_MS) {
    return;  // Rate limit updates
  }
  lastAnimTime = now;

  // Animate probability toward target
  if (animatedProb1 < targetProb1) animatedProb1++;
  else if (animatedProb1 > targetProb1) animatedProb1--;

#if KALSHI_EFFECT == KALSHI_EFFECT_TUG_OF_WAR
  // Animate knot with smooth easing
  float diff = targetKnotPosition - knotPosition;
  if (diff > KNOT_SPEED) {
    knotPosition += KNOT_SPEED;
  } else if (diff < -KNOT_SPEED) {
    knotPosition -= KNOT_SPEED;
  } else {
    knotPosition = targetKnotPosition;
  }
#endif

#if KALSHI_EFFECT == KALSHI_EFFECT_RISING_TIDE
  // Animate wave phases
  wavePhase1 += WAVE_SPEED_1;
  wavePhase2 += WAVE_SPEED_2;
#endif

  // Always redraw (for animations)
#if KALSHI_EFFECT == KALSHI_EFFECT_BASIC
  drawBasicEffect();
#elif KALSHI_EFFECT == KALSHI_EFFECT_TUG_OF_WAR
  drawTugOfWarBar();
#elif KALSHI_EFFECT == KALSHI_EFFECT_RISING_TIDE
  drawRisingTide();
#endif
}
