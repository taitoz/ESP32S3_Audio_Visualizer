#include "serial_cmd.h"
#include "settings.h"
#include "pins_config.h"
#include <ArduinoJson.h>

/*******************************************************************************
 * Serial Command Handler — JSON over USB CDC
 * 
 * Commands (PC → ESP32, one JSON object per line):
 *   {"cmd":"get"}                          → returns full status
 *   {"cmd":"set","brightness":128}         → set one or more fields
 *   {"cmd":"set","viz_mode":1}             → change viz mode
 *   {"cmd":"restart"}                      → restart ESP32
 * 
 * Status push (ESP32 → PC, every ~2 seconds):
 *   {"status":true,"fps":10.2,"free_heap":204800,"uptime":12345,...}
 ******************************************************************************/

extern volatile float fps;

static char serialBuf[512];
static int  serialBufPos = 0;
static unsigned long lastStatusPush = 0;
#define STATUS_PUSH_INTERVAL_MS 2000

// Send full status as JSON line
static void send_status()
{
    StaticJsonDocument<2048> doc;
    doc["status"]          = true;
    doc["viz_mode"]        = settings.viz_mode;
    doc["brightness"]      = settings.brightness;
    doc["auto_brightness"] = settings.auto_brightness;
    doc["brightness_min"]  = settings.brightness_min;
    doc["brightness_max"]  = settings.brightness_max;
    doc["light_gain"]           = settings.light_gain;
    doc["spectrum_sensitivity"] = settings.spectrum_sensitivity;
    doc["spectrum_threshold"]   = settings.spectrum_threshold;
    doc["vu_sensitivity"]       = settings.vu_sensitivity;
    doc["vu_threshold"]         = settings.vu_threshold;
    doc["dac_volume_l"]    = settings.dac_volume_l;
    doc["dac_volume_r"]    = settings.dac_volume_r;
    doc["dac_filter"]      = settings.dac_filter;
    doc["dac_sound_mode"]  = settings.dac_sound_mode;
    doc["dac_mute"]        = (bool)settings.dac_mute;
    doc["mouse_sens"]      = settings.mouse_sens;
    doc["mouse_mode"]      = settings.mouse_mode;
    doc["band_smoothing"]  = settings.band_smoothing;
    doc["peak_fall_rate"]  = settings.peak_fall_rate;
    doc["peak_hold_frames"] = settings.peak_hold_frames;
    doc["vu_attack"]       = settings.vu_attack;
    doc["vu_release"]      = settings.vu_release;
    doc["fps"]             = fps;
    doc["free_heap"]       = ESP.getFreeHeap();
    doc["uptime"]          = millis();

    serializeJson(doc, Serial);
    Serial.println();
}

// Process one complete JSON line
static void process_command(const char *line)
{
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.println("{\"error\":\"invalid json\"}");
        return;
    }

    const char *cmd = doc["cmd"] | "";

    // GET — return status
    if (strcmp(cmd, "get") == 0) {
        send_status();
        return;
    }

    // RESTART
    if (strcmp(cmd, "restart") == 0) {
        Serial.println("{\"ok\":true,\"restarting\":true}");
        Serial.flush();
        delay(200);
        ESP.restart();
        return;
    }

    // SET — update one or more settings
    if (strcmp(cmd, "set") == 0) {
        if (doc["viz_mode"].is<int>()) {
            settings.viz_mode = doc["viz_mode"].as<uint8_t>();
            settings_save_field("viz_mode");
        }
        if (doc["brightness"].is<int>()) {
            settings.brightness = doc["brightness"].as<uint8_t>();
            if (!settings.auto_brightness) {
                analogWrite(TFT_BL, settings.brightness);
            }
            settings_save_field("brightness");
        }
        if (doc["auto_brightness"].is<bool>()) {
            settings.auto_brightness = doc["auto_brightness"].as<bool>();
            if (!settings.auto_brightness) {
                analogWrite(TFT_BL, settings.brightness);
            }
            settings_save_field("auto_bri");
        }
        if (doc["brightness_min"].is<int>()) {
            settings.brightness_min = doc["brightness_min"].as<uint8_t>();
            if (settings.brightness_min < 1) settings.brightness_min = 1;
            settings_save_field("bri_min");
        }
        if (doc["brightness_max"].is<int>()) {
            settings.brightness_max = doc["brightness_max"].as<uint8_t>();
            if (settings.brightness_max < settings.brightness_min) settings.brightness_max = settings.brightness_min;
            settings_save_field("bri_max");
        }
        if (doc["light_gain"].is<float>()) {
            settings.light_gain = doc["light_gain"].as<float>();
            settings_save_field("light_gain");
        }
        if (doc["spectrum_sensitivity"].is<float>()) {
            settings.spectrum_sensitivity = doc["spectrum_sensitivity"].as<float>();
            if (settings.spectrum_sensitivity < 50.0f) settings.spectrum_sensitivity = 50.0f;
            if (settings.spectrum_sensitivity > 10000.0f) settings.spectrum_sensitivity = 10000.0f;
            settings_save_field("spec_sens");
        }
        if (doc["spectrum_threshold"].is<float>()) {
            settings.spectrum_threshold = doc["spectrum_threshold"].as<float>();
            if (settings.spectrum_threshold < 0.0f) settings.spectrum_threshold = 0.0f;
            if (settings.spectrum_threshold > 1.0f) settings.spectrum_threshold = 1.0f;
            settings_save_field("spec_thr");
        }
        if (doc["vu_sensitivity"].is<float>()) {
            settings.vu_sensitivity = doc["vu_sensitivity"].as<float>();
            if (settings.vu_sensitivity < 50.0f) settings.vu_sensitivity = 50.0f;
            if (settings.vu_sensitivity > 10000.0f) settings.vu_sensitivity = 10000.0f;
            settings_save_field("vu_sens");
        }
        if (doc["vu_threshold"].is<float>()) {
            settings.vu_threshold = doc["vu_threshold"].as<float>();
            if (settings.vu_threshold < 0.0f) settings.vu_threshold = 0.0f;
            if (settings.vu_threshold > 1.0f) settings.vu_threshold = 1.0f;
            settings_save_field("vu_thr");
        }
        if (doc["dac_volume_l"].is<int>()) {
            settings.dac_volume_l = doc["dac_volume_l"].as<uint8_t>();
            settings_save_field("dac_vol_l");
        }
        if (doc["dac_volume_r"].is<int>()) {
            settings.dac_volume_r = doc["dac_volume_r"].as<uint8_t>();
            settings_save_field("dac_vol_r");
        }
        if (doc["dac_filter"].is<int>()) {
            settings.dac_filter = doc["dac_filter"].as<uint8_t>();
            settings_save_field("dac_filter");
        }
        if (doc["dac_sound_mode"].is<int>()) {
            settings.dac_sound_mode = doc["dac_sound_mode"].as<uint8_t>();
            settings_save_field("dac_sound");
        }
        if (doc["dac_mute"].is<bool>()) {
            settings.dac_mute = doc["dac_mute"].as<bool>();
            settings_save_field("dac_mute");
        }
        if (doc["mouse_sens"].is<float>()) {
            settings.mouse_sens = doc["mouse_sens"].as<float>();
            settings_save_field("mouse_sens");
        }
        if (doc["mouse_mode"].is<int>()) {
            settings.mouse_mode = doc["mouse_mode"].as<uint8_t>();
            settings_save_field("mouse_mode");
        }
        if (doc["band_smoothing"].is<float>()) {
            settings.band_smoothing = doc["band_smoothing"].as<float>();
            settings_save_field("band_smooth");
        }
        if (doc["peak_fall_rate"].is<float>()) {
            settings.peak_fall_rate = doc["peak_fall_rate"].as<float>();
            settings_save_field("peak_fall");
        }
        if (doc["peak_hold_frames"].is<int>()) {
            settings.peak_hold_frames = doc["peak_hold_frames"].as<uint8_t>();
            settings_save_field("peak_hold");
        }
        if (doc["vu_attack"].is<float>()) {
            settings.vu_attack = doc["vu_attack"].as<float>();
            settings_save_field("vu_attack");
        }
        if (doc["vu_release"].is<float>()) {
            settings.vu_release = doc["vu_release"].as<float>();
            settings_save_field("vu_release");
        }

        // Echo back current state
        send_status();
        return;
    }

    Serial.println("{\"error\":\"unknown cmd\"}");
}

void serial_cmd_init()
{
    serialBufPos = 0;
    Serial.println("{\"ready\":true}");
}

void serial_cmd_poll()
{
    // Read serial data, process line-by-line
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialBufPos > 0) {
                serialBuf[serialBufPos] = '\0';
                process_command(serialBuf);
                serialBufPos = 0;
            }
        } else {
            if (serialBufPos < (int)sizeof(serialBuf) - 1) {
                serialBuf[serialBufPos++] = c;
            }
        }
    }

    // Periodic status push
    unsigned long now = millis();
    if (now - lastStatusPush >= STATUS_PUSH_INTERVAL_MS) {
        send_status();
        lastStatusPush = now;
    }
}
