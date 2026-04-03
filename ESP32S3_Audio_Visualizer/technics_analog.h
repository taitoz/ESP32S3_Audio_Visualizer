#pragma once

#include "technics_common.h"

/*******************************************************************************
 * Technics Analog VU Meter - Classic Needle Style
 * 
 * Static background with moving needle lines
 * Uses traveling sprites for background restoration
 ******************************************************************************/

// ─── Analog Meter Geometry ───────────────────────────────────────────────────
#define ANALOG_CENTER_X     320     // Center of meter arc
#define ANALOG_CENTER_Y     140     // Center of meter arc
#define ANALOG_RADIUS        110     // Radius of meter arc
#define ANALOG_NEEDLE_L      100     // Length of needle line
#define ANALOG_NEEDLE_W      2       // Width of needle line

// ─── Angle Ranges (in degrees) ─────────────────────────────────────────────────
#define ANALOG_MIN_ANGLE    -45     // -20dB position
#define ANALOG_MAX_ANGLE    45      // +6dB position
#define ANALOG_ZERO_ANGLE   0       // 0dB position

// ─── Traveling Sprites ───────────────────────────────────────────────────────
#define ANALOG_TRAVELERS    2       // Number of traveling sprites
#define ANALOG_TRAVELER_W   ANALOG_SPRITE_W
#define ANALOG_TRAVELER_H   ANALOG_SPRITE_H

// ─── Public Interface ───────────────────────────────────────────────────────
void technics_analog_init(TFT_eSPI &tft);
void technics_analog_update(TFT_eSPI &tft, float rmsL, float rmsR);
void technics_analog_cleanup(void);

// ─── Background Image ───────────────────────────────────────────────────────
// Should be defined as:
// extern const uint16_t technics_analog_bg[SCREEN_WIDTH * SCREEN_HEIGHT] PROGMEM;
