#pragma once

#include <Arduino.h>
#include <RTClib.h>

/*******************************************************************************
 * RTC DS3231 - Real-Time Clock Module
 * 
 * Provides accurate timekeeping with battery backup.
 * Updates global time variable once per second via FreeRTOS task.
 ******************************************************************************/

// Global time structure (volatile for cross-core access)
typedef struct {
    uint8_t hour;      // 0-23
    uint8_t minute;    // 0-59
    uint8_t second;    // 0-59
    uint8_t day;       // 1-31
    uint8_t month;     // 1-12
    uint16_t year;     // e.g., 2026
    uint8_t dayOfWeek; // 0=Sunday, 6=Saturday
    bool valid;        // true if RTC is running and time is valid
} RTCTime;

extern volatile RTCTime currentTime;

// RTC API
void rtc_init();
void rtc_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
void rtc_update_time();  // Called by update_time_task
bool rtc_is_running();
float rtc_get_temperature();  // DS3231 has built-in temperature sensor
