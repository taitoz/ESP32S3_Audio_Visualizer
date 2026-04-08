#pragma once

#include <Arduino.h>
#include <Preferences.h>

/*******************************************************************************
 * Settings Structure — Runtime configuration with NVS persistence
 * 
 * All settings are stored in ESP32 NVS (Preferences) and loaded at startup.
 * The settings struct is volatile since it can be accessed from multiple cores.
 ******************************************************************************/


typedef struct {
    // Visualization
    uint8_t viz_mode;
    
    // Display
    uint8_t brightness;
    bool auto_brightness;
    uint8_t brightness_min;
    uint8_t brightness_max;
    float light_gain;
    
    // Audio ADC
    float adc_sensitivity;
    float noise_threshold;  // Minimum signal level to display (filters ADC noise)
    
    // DAC (AK4493)
    uint8_t dac_volume_l;
    uint8_t dac_volume_r;
    uint8_t dac_filter;
    uint8_t dac_sound_mode;
    bool dac_mute;
    
    // Mouse (USB HID)
    float mouse_sens;
    uint8_t mouse_mode;
    
    // Spectrum processing
    float band_smoothing;
    float peak_fall_rate;
    uint8_t peak_hold_frames;
    
    // VU meter
    float vu_attack;
    float vu_release;
} Settings;

// Global settings instance (volatile for cross-core access)
extern volatile Settings settings;

// Settings API
void settings_init();
void settings_save();
void settings_save_field(const char* field);
