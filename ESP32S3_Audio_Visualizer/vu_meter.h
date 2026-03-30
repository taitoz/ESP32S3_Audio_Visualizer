#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pins_config.h"

/*******************************************************************************
 * VU Meter Visualizations — multiple styles, touch-switchable
 * 
 * All draw functions expect dB level in range roughly -60..0 dB
 * and RMS/peak values from audio_sampling module.
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
void vu_meter_update(float rms, float peak);  // call each frame with raw RMS and peak
void vu_meter_draw_needle(TFT_eSprite &spr);
void vu_meter_draw_led_ladder(TFT_eSprite &spr);
void vu_meter_draw_retro(TFT_eSprite &spr);
void vu_meter_draw(TFT_eSprite &spr, VUStyle style);  // dispatch to correct style

float vu_get_db();       // current smoothed dB level
float vu_get_peak_db();  // current peak dB level
