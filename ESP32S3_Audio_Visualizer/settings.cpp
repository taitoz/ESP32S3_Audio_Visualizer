#include "settings.h"
#include <Preferences.h>

/*******************************************************************************
 * Settings — NVS persistence using ESP32 Preferences library
 ******************************************************************************/

volatile Settings settings;
static Preferences prefs;

void settings_init()
{
    // Defaults
    settings.viz_mode        = 0;       // VIS_SPECTRUM
    settings.brightness      = 128;     // 50% default brightness
    settings.adc_sensitivity = 300.0f;  // spectrum divisor
    settings.dac_volume_l    = 0x00;    // 0 dB
    settings.dac_volume_r    = 0x00;    // 0 dB
    settings.dac_filter      = 0;       // sharp roll-off
    settings.dac_sound_mode  = 0;
    settings.dac_mute        = false;
    settings.mouse_sens      = 1.0f;
    settings.mouse_mode      = 0;       // touchpad
    settings.auto_brightness = false;
    settings.brightness_min  = 10;      // don't go fully dark
    settings.brightness_max  = 255;
    settings.light_gain      = 1.0f;
    settings.band_smoothing  = 0.7f;
    settings.peak_fall_rate  = 0.5f;
    settings.peak_hold_frames = 15;
    settings.vu_attack       = 0.3f;
    settings.vu_release      = 0.5f;

    // Load saved values (if they exist)
    prefs.begin("config", true);  // read-only
    settings.viz_mode        = prefs.getUChar("viz_mode",    settings.viz_mode);
    if (settings.viz_mode >= 2) settings.viz_mode = 0;  // Clamp to valid modes (VIS_EQ=0, VIS_VU=1)
    settings.brightness      = prefs.getUChar("brightness",  settings.brightness);
    settings.adc_sensitivity = prefs.getFloat("adc_sens",    settings.adc_sensitivity);
    settings.dac_volume_l    = prefs.getUChar("dac_vol_l",   settings.dac_volume_l);
    settings.dac_volume_r    = prefs.getUChar("dac_vol_r",   settings.dac_volume_r);
    settings.dac_filter      = prefs.getUChar("dac_filter",  settings.dac_filter);
    settings.dac_sound_mode  = prefs.getUChar("dac_sound",   settings.dac_sound_mode);
    settings.dac_mute        = prefs.getBool("dac_mute",     settings.dac_mute);
    settings.mouse_sens      = prefs.getFloat("mouse_sens",  settings.mouse_sens);
    settings.mouse_mode      = prefs.getUChar("mouse_mode",  settings.mouse_mode);
    settings.auto_brightness = prefs.getBool("auto_bri",      settings.auto_brightness);
    settings.brightness_min  = prefs.getUChar("bri_min",      settings.brightness_min);
    settings.brightness_max  = prefs.getUChar("bri_max",      settings.brightness_max);
    settings.light_gain      = prefs.getFloat("light_gain",   settings.light_gain);
    settings.band_smoothing  = prefs.getFloat("band_smooth",  settings.band_smoothing);
    settings.peak_fall_rate  = prefs.getFloat("peak_fall",    settings.peak_fall_rate);
    settings.peak_hold_frames = prefs.getUChar("peak_hold",   settings.peak_hold_frames);
    settings.vu_attack       = prefs.getFloat("vu_attack",    settings.vu_attack);
    settings.vu_release      = prefs.getFloat("vu_release",   settings.vu_release);
    prefs.end();
}

void settings_save()
{
    prefs.begin("config", false);  // read-write
    prefs.putUChar("viz_mode",    settings.viz_mode);
    prefs.putUChar("brightness",  settings.brightness);
    prefs.putFloat("adc_sens",    settings.adc_sensitivity);
    prefs.putUChar("dac_vol_l",   settings.dac_volume_l);
    prefs.putUChar("dac_vol_r",   settings.dac_volume_r);
    prefs.putUChar("dac_filter",  settings.dac_filter);
    prefs.putUChar("dac_sound",   settings.dac_sound_mode);
    prefs.putBool("dac_mute",     settings.dac_mute);
    prefs.putFloat("mouse_sens",  settings.mouse_sens);
    prefs.putUChar("mouse_mode",  settings.mouse_mode);
    prefs.putBool("auto_bri",     settings.auto_brightness);
    prefs.putUChar("bri_min",     settings.brightness_min);
    prefs.putUChar("bri_max",     settings.brightness_max);
    prefs.putFloat("light_gain",  settings.light_gain);
    prefs.putFloat("band_smooth", settings.band_smoothing);
    prefs.putFloat("peak_fall",   settings.peak_fall_rate);
    prefs.putUChar("peak_hold",   settings.peak_hold_frames);
    prefs.putFloat("vu_attack",   settings.vu_attack);
    prefs.putFloat("vu_release",  settings.vu_release);
    prefs.end();
}

void settings_save_field(const char* field)
{
    prefs.begin("config", false);
    if (strcmp(field, "viz_mode") == 0)        prefs.putUChar("viz_mode",   settings.viz_mode);
    else if (strcmp(field, "brightness") == 0)  prefs.putUChar("brightness", settings.brightness);
    else if (strcmp(field, "adc_sens") == 0)    prefs.putFloat("adc_sens",   settings.adc_sensitivity);
    else if (strcmp(field, "dac_vol_l") == 0)   prefs.putUChar("dac_vol_l",  settings.dac_volume_l);
    else if (strcmp(field, "dac_vol_r") == 0)   prefs.putUChar("dac_vol_r",  settings.dac_volume_r);
    else if (strcmp(field, "dac_filter") == 0)  prefs.putUChar("dac_filter", settings.dac_filter);
    else if (strcmp(field, "dac_sound") == 0)   prefs.putUChar("dac_sound",  settings.dac_sound_mode);
    else if (strcmp(field, "dac_mute") == 0)    prefs.putBool("dac_mute",    settings.dac_mute);
    else if (strcmp(field, "mouse_sens") == 0)  prefs.putFloat("mouse_sens", settings.mouse_sens);
    else if (strcmp(field, "mouse_mode") == 0)  prefs.putUChar("mouse_mode", settings.mouse_mode);
    else if (strcmp(field, "auto_bri") == 0)    prefs.putBool("auto_bri",    settings.auto_brightness);
    else if (strcmp(field, "bri_min") == 0)     prefs.putUChar("bri_min",    settings.brightness_min);
    else if (strcmp(field, "bri_max") == 0)     prefs.putUChar("bri_max",    settings.brightness_max);
    else if (strcmp(field, "light_gain") == 0)  prefs.putFloat("light_gain", settings.light_gain);
    else if (strcmp(field, "band_smooth") == 0) prefs.putFloat("band_smooth",settings.band_smoothing);
    else if (strcmp(field, "peak_fall") == 0)   prefs.putFloat("peak_fall",  settings.peak_fall_rate);
    else if (strcmp(field, "peak_hold") == 0)   prefs.putUChar("peak_hold",  settings.peak_hold_frames);
    else if (strcmp(field, "vu_attack") == 0)   prefs.putFloat("vu_attack",  settings.vu_attack);
    else if (strcmp(field, "vu_release") == 0)  prefs.putFloat("vu_release", settings.vu_release);
    prefs.end();
}
