#include "settings.h"
#include <Preferences.h>

/*******************************************************************************
 * Settings — NVS persistence using ESP32 Preferences library
 ******************************************************************************/

Settings settings;
static Preferences prefs;

void settings_init()
{
    // Defaults
    settings.viz_mode        = 0;       // VIS_SPECTRUM
    settings.brightness      = 255;     // full brightness
    settings.adc_sensitivity = 300.0f;  // spectrum divisor
    settings.dac_volume_l    = 0x00;    // 0 dB
    settings.dac_volume_r    = 0x00;    // 0 dB
    settings.dac_filter      = 0;       // sharp roll-off
    settings.dac_sound_mode  = 0;
    settings.dac_mute        = false;
    settings.mouse_sens      = 1.0f;
    settings.mouse_mode      = 0;       // touchpad

    // Load saved values (if they exist)
    prefs.begin("config", true);  // read-only
    settings.viz_mode        = prefs.getUChar("viz_mode",    settings.viz_mode);
    settings.brightness      = prefs.getUChar("brightness",  settings.brightness);
    settings.adc_sensitivity = prefs.getFloat("adc_sens",    settings.adc_sensitivity);
    settings.dac_volume_l    = prefs.getUChar("dac_vol_l",   settings.dac_volume_l);
    settings.dac_volume_r    = prefs.getUChar("dac_vol_r",   settings.dac_volume_r);
    settings.dac_filter      = prefs.getUChar("dac_filter",  settings.dac_filter);
    settings.dac_sound_mode  = prefs.getUChar("dac_sound",   settings.dac_sound_mode);
    settings.dac_mute        = prefs.getBool("dac_mute",     settings.dac_mute);
    settings.mouse_sens      = prefs.getFloat("mouse_sens",  settings.mouse_sens);
    settings.mouse_mode      = prefs.getUChar("mouse_mode",  settings.mouse_mode);
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
    prefs.end();
}
