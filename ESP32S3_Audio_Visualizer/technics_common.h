#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

/*******************************************************************************
 * Technics VFD Legacy - Common Constants and Utilities
 * 
 * Shared resources for all Technics visualization modes using Dirty Rectangles
 ******************************************************************************/

// ─── VFD Colors (RGB565) ───────────────────────────────────────────────────────
#define VFD_CYAN_FULL     0x07FF   // Bright cyan (R:0, G:63, B:31)
#define VFD_CYAN_HALF     0x03EF   // Half brightness cyan (R:0, G:31, B:15)
#define VFD_AMBER_FULL    0xFDA0   // Bright amber (R:31, G:61, B:0)
#define VFD_AMBER_HALF    0x7B20   // Half brightness amber (R:15, G:30, B:0)
#define VFD_BLACK         0x0000   // Background

// ─── Display Dimensions ───────────────────────────────────────────────────────
#define SCREEN_WIDTH      640
#define SCREEN_HEIGHT     180

// ─── Dirty Rectangle Sprites ───────────────────────────────────────────────────
// Small sprites in SRAM for fast updates (avoid PSRAM access)
#define EQ_BAND_SPRITE_W   40
#define EQ_BAND_SPRITE_H   120
#define EQ_BANDS_COUNT     8

#define VU_METER_SPRITE_W  450
#define VU_METER_SPRITE_H  20

#define ANALOG_SPRITE_W    60
#define ANALOG_SPRITE_H    8

// ─── Background Images (PROGMEM) ───────────────────────────────────────────────
// These should be defined in separate header files:
// extern const uint16_t technics_eq_bg[SCREEN_WIDTH * SCREEN_HEIGHT] PROGMEM;
// extern const uint16_t technics_vu_bg[SCREEN_WIDTH * SCREEN_HEIGHT] PROGMEM;
// extern const uint16_t technics_analog_bg[SCREEN_WIDTH * SCREEN_HEIGHT] PROGMEM;

// ─── Utility Functions ─────────────────────────────────────────────────────────

// Get color based on dB level (0dB = 0.8f normalized)
inline uint16_t get_vfd_color(float value, float threshold = 0.8f) {
    return (value >= threshold) ? VFD_CYAN_FULL : VFD_AMBER_FULL;
}

// Get half-brightness color for partial segments
inline uint16_t get_vfd_half_color(float value, float threshold = 0.8f) {
    return (value >= threshold) ? VFD_CYAN_HALF : VFD_AMBER_HALF;
}

// Calculate segment brightness based on fractional part
inline uint16_t get_segment_brightness(float fractional, float threshold = 0.8f) {
    if (fractional <= 0.25f) return VFD_BLACK;
    else if (fractional <= 0.75f) return get_vfd_half_color(threshold);
    else return get_vfd_color(threshold);
}

// Apply EMA ballistics with different attack/release coefficients
inline float apply_ema(float current, float filtered, float attack_alpha, float release_alpha) {
    return (current > filtered) ? 
        (attack_alpha * current + (1.0f - attack_alpha) * filtered) :
        (release_alpha * current + (1.0f - release_alpha) * filtered);
}

// Logarithmic scaling for better VU response
inline float log_scale(float value) {
    return value * value;  // Simple square for better low-level response
}

// ─── Ballistics Settings ───────────────────────────────────────────────────────
#define EQ_ATTACK_ALPHA     0.7f    // Fast attack for EQ
#define EQ_RELEASE_ALPHA    0.3f    // Medium release for EQ

#define VU_ATTACK_ALPHA    0.9f    // Instant attack for VU
#define VU_RELEASE_ALPHA    0.15f   // Very slow "viscous" release

#define ANALOG_ATTACK_ALPHA 0.6f    // Medium attack for analog
#define ANALOG_RELEASE_ALPHA 0.4f   // Medium release for analog

// ─── Peak Hold Settings ───────────────────────────────────────────────────────
#define PEAK_HOLD_MS       500      // Peak hold duration
#define PEAK_FADE_MS       300      // Peak fade duration
