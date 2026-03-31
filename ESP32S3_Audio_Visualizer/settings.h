#pragma once
#include <stdint.h>

/*******************************************************************************
 * Settings — volatile runtime config + NVS persistence
 * 
 * Written by: Web server handlers (Core 0), touch task (Core 0)
 * Read by:    Audio/display task (Core 1)
 * All fields are volatile for cross-core safety.
 ******************************************************************************/

struct Settings {
    volatile uint8_t viz_mode;          // VisMode enum value
    volatile uint8_t brightness;        // 0–255 PWM for TFT_BL
    volatile float   adc_sensitivity;   // spectrum divisor (default 300.0)
    volatile uint8_t dac_volume_l;      // AK4493 left volume (0x00=0dB, 0xFF=mute)
    volatile uint8_t dac_volume_r;      // AK4493 right volume
    volatile uint8_t dac_filter;        // AK4493 filter mode (0–4)
    volatile uint8_t dac_sound_mode;    // AK4493 sound control
    volatile bool    dac_mute;          // soft mute
    volatile float   mouse_sens;        // USB HID mouse sensitivity
    volatile uint8_t mouse_mode;        // 0=touchpad, 1=gyro
    // Auto-brightness via ambient light sensor
    volatile bool    auto_brightness;   // enable auto-brightness adjustment
    volatile uint8_t brightness_min;    // minimum PWM when dark (0–255)
    volatile uint8_t brightness_max;    // maximum PWM when bright (0–255)
    volatile float   light_gain;        // light sensor gain multiplier (default 1.0)
    // Spectrum analyzer tunables
    volatile float   band_smoothing;    // exponential smoothing (0=instant, 1=frozen, default 0.7)
    volatile float   peak_fall_rate;    // peak dot fall speed per frame (default 0.5)
    volatile uint8_t peak_hold_frames;  // frames to hold peak before falling (default 15)
    // VU meter tunables
    volatile float   vu_attack;         // VU attack coefficient (default 0.3)
    volatile float   vu_release;        // VU release coefficient (default 0.5)
};

extern Settings settings;

void settings_init();       // load from NVS (or defaults)
void settings_save();       // persist current settings to NVS
void settings_save_field(const char* field);  // persist a single field
