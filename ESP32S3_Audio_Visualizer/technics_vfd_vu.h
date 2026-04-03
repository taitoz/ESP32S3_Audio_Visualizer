#pragma once

#include "technics_common.h"

/*******************************************************************************
 * Technics VFD VU Meter - RS-TR373 Cassette Deck Style
 * 
 * Thin horizontal segmented bars with peak hold functionality
 * Instant attack, very slow "viscous" release
 ******************************************************************************/

// ─── VU Geometry ───────────────────────────────────────────────────────────
#define VU_BAR_WIDTH       400     // Length of VU bar
#define VU_BAR_HEIGHT      8       // Height of VU bar
#define VU_SEGMENT_WIDTH   6       // Width of individual segment
#define VU_SEGMENT_GAP     2       // Gap between segments
#define VU_MAX_SEGMENTS    ((VU_BAR_WIDTH + VU_SEGMENT_GAP) / (VU_SEGMENT_WIDTH + VU_SEGMENT_GAP))

#define VU_LEFT_X          120     // Left channel position
#define VU_LEFT_Y          60      // Left channel Y position
#define VU_RIGHT_X         120     // Right channel position
#define VU_RIGHT_Y         100     // Right channel Y position

// ─── Peak Hold State ───────────────────────────────────────────────────────
typedef struct {
    bool active;
    unsigned long start_time;
    float brightness;  // 1.0 = full, 0.0 = off
} peak_hold_t;

// ─── Public Interface ───────────────────────────────────────────────────────
void technics_vfd_vu_init(TFT_eSPI &tft);
void technics_vfd_vu_update(TFT_eSPI &tft, float rmsL, float rmsR);
void technics_vfd_vu_cleanup(void);

// ─── Background Image ───────────────────────────────────────────────────────
// Should be defined as:
// extern const uint16_t technics_vu_bg[SCREEN_WIDTH * SCREEN_HEIGHT] PROGMEM;
