#ifndef KALSHI_DISPLAY_H
#define KALSHI_DISPLAY_H

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// Effect modes
#define KALSHI_EFFECT_BASIC       0  // Two rows with individual probability bars
#define KALSHI_EFFECT_TUG_OF_WAR  1  // Single bar showing tug-of-war between teams
#define KALSHI_EFFECT_RISING_TIDE 2  // Animated waves battling from each side

// Select which effect to use
#define KALSHI_EFFECT KALSHI_EFFECT_RISING_TIDE

// Sports matchup data structures
struct SportsTeam {
  const char* name;
  uint8_t probability;  // 0-100
  uint16_t color;       // RGB565 team color
};

struct SportsMatchup {
  SportsTeam team1;
  SportsTeam team2;
};

// Initialize Kalshi display (call after initDisplay)
void initKalshiDisplay(MatrixPanel_I2S_DMA* display);

// Set the current matchup data (use for initial setup)
void setSportsMatchup(SportsMatchup* matchup);

// Update target probabilities (animates smoothly from current values)
void setTargetProbability(int team1Probability);

// Call from loop() to animate
void updateSportsDisplay();

#endif
