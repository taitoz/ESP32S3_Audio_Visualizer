#include "serial_cmd.h"
#include "settings.h"
#include "pins_config.h"
#include "rtc_time.h"
#include "gearvr_controller.h"
#include <ArduinoJson.h>

/*******************************************************************************
 * Serial Command Handler — JSON over USB CDC
 * 
 * Commands (PC → ESP32, one JSON object per line):
 *   {"cmd":"get"}                                                    → returns full status
 *   {"cmd":"status"}                                                 → alias for get
 *   {"cmd":"set","brightness":128}                                   → set one or more fields
 *   {"cmd":"set","dac_volume_l":165,"dac_filter":0}                  → set multiple fields
 *   {"cmd":"rtc_set","year":2026,"month":4,"day":14,"hour":12,"min":0,"sec":0} → set RTC time
 *   {"cmd":"gearvr_connect"}                                         → connect to Gear VR (async)
 *   {"cmd":"gearvr_disconnect"}                                      → disconnect from Gear VR
 *   {"cmd":"restart"}                                                → restart ESP32
 * 
 * Responses (all commands send immediate response):
 *   {"ok":true,"action":"settings_updated"}                          → settings changed
 *   {"status":true,"fps":7.1,"free_heap":157500,...}                 → full status (after set/get/status)
 *   {"ok":true,"action":"rtc_set","time":"2026-04-14 12:00:00"}      → RTC time set
 *   {"ok":true,"action":"gearvr_connect_queued"}                     → BLE connect queued
 *   {"error":"unknown cmd"}                                          → invalid command
 * 
 * Note: Status is sent ONLY in response to commands, not automatically.
 ******************************************************************************/

extern volatile float fps;

static char serialBuf[512];
static int  serialBufPos = 0;

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
    
    // RTC status
    doc["rtc_time"]        = currentTime.hour * 10000 + currentTime.minute * 100 + currentTime.second;
    doc["rtc_date"]        = currentTime.year * 10000 + currentTime.month * 100 + currentTime.day;
    // doc["rtc_temp"]     = rtc_get_temperature();  // DISABLED - blocks I2C, causes watchdog
    doc["rtc_valid"]       = currentTime.valid;
    
    // Gear VR status
    doc["gearvr_connected"] = gearvr_is_connected();
    doc["gearvr_battery"]   = gearVR.batteryLevel;

    serializeJson(doc, Serial);
    Serial.println();
}

// Process one complete JSON line
static void process_command(const char *line)
{
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.printf("{\"error\":\"invalid json\",\"line\":\"%s\"}\n", line);
        Serial.flush();
        return;
    }

    const char *cmd = doc["cmd"] | "";

    // GET — return status
    if (strcmp(cmd, "get") == 0) {
        send_status();
        Serial.flush();
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

    // RTC_SET — set RTC time
    if (strcmp(cmd, "rtc_set") == 0) {
        uint16_t year = doc["year"] | 2026;
        uint8_t month = doc["month"] | 1;
        uint8_t day = doc["day"] | 1;
        uint8_t hour = doc["hour"] | 0;
        uint8_t minute = doc["min"] | 0;
        uint8_t second = doc["sec"] | 0;
        
        rtc_set_time(year, month, day, hour, minute, second);
        Serial.printf("{\"ok\":true,\"action\":\"rtc_set\",\"time\":\"%04d-%02d-%02d %02d:%02d:%02d\"}\n",
                      year, month, day, hour, minute, second);
        Serial.flush();
        return;
    }

    // SET — update one or more settings
    if (strcmp(cmd, "set") == 0) {
        bool changed = false;
        
        if (doc["viz_mode"].is<int>()) {
            settings.viz_mode = doc["viz_mode"].as<uint8_t>();
            settings_save_field("viz_mode");
            changed = true;
        }
        if (doc["brightness"].is<int>()) {
            settings.brightness = doc["brightness"].as<uint8_t>();
            if (!settings.auto_brightness) {
                analogWrite(TFT_BL, settings.brightness);
            }
            settings_save_field("brightness");
            changed = true;
        }
        if (doc["auto_brightness"].is<bool>()) {
            settings.auto_brightness = doc["auto_brightness"].as<bool>();
            if (!settings.auto_brightness) {
                analogWrite(TFT_BL, settings.brightness);
            }
            settings_save_field("auto_bri");
            changed = true;
        }
        if (doc["brightness_min"].is<int>()) {
            settings.brightness_min = doc["brightness_min"].as<uint8_t>();
            if (settings.brightness_min < 1) settings.brightness_min = 1;
            settings_save_field("bri_min");
            changed = true;
        }
        if (doc["brightness_max"].is<int>()) {
            settings.brightness_max = doc["brightness_max"].as<uint8_t>();
            if (settings.brightness_max < settings.brightness_min) settings.brightness_max = settings.brightness_min;
            settings_save_field("bri_max");
            changed = true;
        }
        if (doc["light_gain"].is<float>()) {
            settings.light_gain = doc["light_gain"].as<float>();
            settings_save_field("light_gain");
            changed = true;
        }
        if (doc["spectrum_sensitivity"].is<float>()) {
            settings.spectrum_sensitivity = doc["spectrum_sensitivity"].as<float>();
            if (settings.spectrum_sensitivity < 50.0f) settings.spectrum_sensitivity = 50.0f;
            if (settings.spectrum_sensitivity > 10000.0f) settings.spectrum_sensitivity = 10000.0f;
            settings_save_field("spec_sens");
            changed = true;
        }
        if (doc["spectrum_threshold"].is<float>()) {
            settings.spectrum_threshold = doc["spectrum_threshold"].as<float>();
            if (settings.spectrum_threshold < 0.0f) settings.spectrum_threshold = 0.0f;
            if (settings.spectrum_threshold > 1.0f) settings.spectrum_threshold = 1.0f;
            settings_save_field("spec_thr");
            changed = true;
        }
        if (doc["vu_sensitivity"].is<float>()) {
            settings.vu_sensitivity = doc["vu_sensitivity"].as<float>();
            if (settings.vu_sensitivity < 50.0f) settings.vu_sensitivity = 50.0f;
            if (settings.vu_sensitivity > 10000.0f) settings.vu_sensitivity = 10000.0f;
            settings_save_field("vu_sens");
            changed = true;
        }
        if (doc["vu_threshold"].is<float>()) {
            settings.vu_threshold = doc["vu_threshold"].as<float>();
            if (settings.vu_threshold < 0.0f) settings.vu_threshold = 0.0f;
            if (settings.vu_threshold > 1.0f) settings.vu_threshold = 1.0f;
            settings_save_field("vu_thr");
            changed = true;
        }
        if (doc["dac_volume_l"].is<int>()) {
            settings.dac_volume_l = doc["dac_volume_l"].as<uint8_t>();
            settings_save_field("dac_vol_l");
            changed = true;
        }
        if (doc["dac_volume_r"].is<int>()) {
            settings.dac_volume_r = doc["dac_volume_r"].as<uint8_t>();
            settings_save_field("dac_vol_r");
            changed = true;
        }
        if (doc["dac_filter"].is<int>()) {
            settings.dac_filter = doc["dac_filter"].as<uint8_t>();
            settings_save_field("dac_filter");
            changed = true;
        }
        if (doc["dac_sound_mode"].is<int>()) {
            settings.dac_sound_mode = doc["dac_sound_mode"].as<uint8_t>();
            settings_save_field("dac_sound");
            changed = true;
        }
        if (doc["dac_mute"].is<bool>()) {
            settings.dac_mute = doc["dac_mute"].as<bool>();
            settings_save_field("dac_mute");
            changed = true;
        }
        if (doc["mouse_sens"].is<float>()) {
            settings.mouse_sens = doc["mouse_sens"].as<float>();
            settings_save_field("mouse_sens");
            changed = true;
        }
        if (doc["mouse_mode"].is<int>()) {
            settings.mouse_mode = doc["mouse_mode"].as<uint8_t>();
            settings_save_field("mouse_mode");
            changed = true;
        }
        if (doc["band_smoothing"].is<float>()) {
            settings.band_smoothing = doc["band_smoothing"].as<float>();
            settings_save_field("band_smooth");
            changed = true;
        }
        if (doc["peak_fall_rate"].is<float>()) {
            settings.peak_fall_rate = doc["peak_fall_rate"].as<float>();
            settings_save_field("peak_fall");
            changed = true;
        }
        if (doc["peak_hold_frames"].is<int>()) {
            settings.peak_hold_frames = doc["peak_hold_frames"].as<uint8_t>();
            settings_save_field("peak_hold");
            changed = true;
        }
        if (doc["vu_attack"].is<float>()) {
            settings.vu_attack = doc["vu_attack"].as<float>();
            settings_save_field("vu_attack");
            changed = true;
        }
        if (doc["vu_release"].is<float>()) {
            settings.vu_release = doc["vu_release"].as<float>();
            settings_save_field("vu_release");
            changed = true;
        }

        // Send confirmation
        if (changed) {
            Serial.println("{\"ok\":true,\"action\":\"settings_updated\"}");
            Serial.flush();
        }
        
        // Echo back current state
        send_status();
        Serial.flush();
        return;
    }
    
    // GEARVR_CONNECT (async — request handled by bleRtcTask on Core 0)
    if (strcmp(cmd, "gearvr_connect") == 0) {
        extern volatile bool bleConnectRequested;
        bleConnectRequested = true;
        Serial.println("{\"ok\":true,\"action\":\"gearvr_connect_queued\"}");
        Serial.flush();
        return;
    }
    
    // GEARVR_DISCONNECT (async)
    if (strcmp(cmd, "gearvr_disconnect") == 0) {
        extern volatile bool bleDisconnectRequested;
        bleDisconnectRequested = true;
        Serial.println("{\"ok\":true,\"action\":\"gearvr_disconnect_queued\"}");
        Serial.flush();
        return;
    }
    
    // STATUS (alias for GET)
    if (strcmp(cmd, "status") == 0) {
        send_status();
        Serial.flush();
        return;
    }

    Serial.println("{\"error\":\"unknown cmd\"}");
    Serial.flush();
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
    
    // No automatic status push - only send status in response to commands
}
