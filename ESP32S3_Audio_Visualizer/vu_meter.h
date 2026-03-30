#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pins_config.h"
#include "audio_sampling.h"

/*******************************************************************************
 * VU Meter Visualizations — Stereo (L + R), multiple styles, touch-switchable
 * 
 * All draw functions use per-channel dB levels in range roughly -60..+3 dB
 ******************************************************************************/

// VU meter ballistics
#define VU_ATTACK_COEFF    0.3f    // fast attack
#define VU_RELEASE_COEFF   0.05f   // slow release (classic VU behavior)

// Style identifiers
enum VUStyle {
    VU_STYLE_NEEDLE = 0,    // Classic analog needle meter
    VU_STYLE_LED_LADDER,    // Horizontal segmented LED bar
    VU_STYLE_RETRO,         // Retro analog dual-meter with glow
    VU_STYLE_COUNT          // sentinel — number of styles
};

void vu_meter_init();
void vu_meter_update(float rmsL, float peakL, float rmsR, float peakR);  // stereo update
void vu_meter_draw_needle(TFT_eSprite &spr);
void vu_meter_draw_led_ladder(TFT_eSprite &spr);
void vu_meter_draw_retro(TFT_eSprite &spr);
void vu_meter_draw(TFT_eSprite &spr, VUStyle style);  // dispatch to correct style

float vu_get_db(int ch);       // current smoothed dB level (CH_LEFT or CH_RIGHT)
float vu_get_peak_db(int ch);  // current peak dB level (CH_LEFT or CH_RIGHT)
