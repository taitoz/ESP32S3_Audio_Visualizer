#pragma once

#include <Arduino.h>

/*******************************************************************************
 * Gear VR Controller - BLE HID Host
 * 
 * Connects to Samsung Gear VR Controller via BLE (NimBLE stack).
 * MAC Address: 2C:BA:BA:2A:D4:05
 * 
 * Features:
 * - Touchpad (X, Y coordinates)
 * - Buttons (Trigger, Back, Home, Volume Up/Down, Touchpad Click)
 * - Battery level monitoring
 * - IMU data (accelerometer, gyroscope, magnetometer)
 * 
 * Oculus Proprietary Service: 4f63756c-7573-2054-6872-6565646f6f6d
 ******************************************************************************/

// Gear VR Controller state
typedef struct {
    // Touchpad
    uint16_t touchX;        // 0-315 (raw)
    uint16_t touchY;        // 0-315 (raw)
    bool touchActive;       // true if finger on touchpad
    
    // Buttons
    bool triggerPressed;
    bool touchpadClicked;
    bool backPressed;
    bool homePressed;
    bool volumeUpPressed;
    bool volumeDownPressed;
    
    // Battery
    uint8_t batteryLevel;   // 0-100%
    
    // IMU (if needed)
    int16_t accelX, accelY, accelZ;
    int16_t gyroX, gyroY, gyroZ;
    int16_t magX, magY, magZ;
    
    // Connection status
    bool connected;
    uint32_t lastUpdateMs;
} GearVRState;

extern volatile GearVRState gearVR;

// Gear VR API
void gearvr_init();
void gearvr_connect();
void gearvr_disconnect();
bool gearvr_is_connected();
void gearvr_update();  // Called periodically to process BLE events

// USB HID Mouse integration
void gearvr_update_mouse();  // Call this in loop() to update USB HID Mouse
void gearvr_get_mouse_delta(int16_t *dx, int16_t *dy);  // Get relative movement
bool gearvr_get_mouse_buttons(bool *left, bool *right, bool *middle);  // Get button states
