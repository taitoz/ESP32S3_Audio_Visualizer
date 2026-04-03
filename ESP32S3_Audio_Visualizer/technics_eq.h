#pragma once

#include "technics_common.h"

/*******************************************************************************
 * Technics EQ Display - SH-GE70 Style Equalizer
 * 
 * 8-band spectrum analyzer with dual-brightness segments
 * Uses Dirty Rectangles with small SRAM sprites for 30+ FPS
 ******************************************************************************/

// ─── EQ Geometry ─────────────────────────────────────────────────────────────
#define EQ_BANDS           8       // Number of frequency bands
#define EQ_BAND_WIDTH      35      // Width of each band
#define EQ_BAND_GAP        10      // Gap between bands
#define EQ_SEGMENT_HEIGHT  8       // Height of one segment
#define EQ_SEGMENT_GAP     2       // Gap between segments
#define EQ_MAX_SEGMENTS    12      // Maximum segments per band

#define EQ_START_X         140     // Starting X position
#define EQ_START_Y         40      // Starting Y position (bottom)

// ─── Frequency Band Mapping ───────────────────────────────────────────────────
// Logarithmic frequency distribution for 8 bands
extern const float eq_frequencies[EQ_BANDS];  // Hz values for each band

// ─── Public Interface ───────────────────────────────────────────────────────
void technics_eq_init(TFT_eSPI &tft);
void technics_eq_update(TFT_eSPI &tft, const float *bandValues);
void technics_eq_cleanup(void);

// ─── Background Image ───────────────────────────────────────────────────────
// Should be defined as:
// extern const uint16_t technics_eq_bg[SCREEN_WIDTH * SCREEN_HEIGHT] PROGMEM;
