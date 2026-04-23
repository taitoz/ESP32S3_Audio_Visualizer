#include "rtc_time.h"
#include "pins_config.h"
#include <Wire.h>

/*******************************************************************************
 * RTC DS3231 Implementation
 ******************************************************************************/

volatile RTCTime currentTime = {0, 0, 0, 1, 1, 2026, 0, false, 0.0f};

static RTC_DS3231 rtc;
static bool rtcAvailable = false;     // Track if RTC is present

void rtc_init()
{
    // Use the shared main Wire bus (initialized with Qwiic pins 18/17 in setup()).
    // Both the RTC and AK4493 DAC live on this bus.
    Serial.printf("RTC using shared I2C bus: SDA=%d, SCL=%d\n", I2C_SDA, I2C_SCL);
    
    if (!rtc.begin(&Wire)) {
        Serial.println("RTC DS3231 not found - continuing without RTC");
        currentTime.valid = false;
        rtcAvailable = false;
        return;
    }
    
    rtcAvailable = true;
    
    if (rtc.lostPower()) {
        Serial.println("RTC lost power, setting default time!");
        // Set to compile time as fallback
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    
    // Initial time read
    rtc_update_time();
    
    Serial.printf("RTC DS3231 initialized. Time: %02d:%02d:%02d\n", 
                  currentTime.hour, currentTime.minute, currentTime.second);
}

void rtc_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    if (!rtcAvailable) {
        Serial.println("RTC not available - cannot set time");
        return;
    }
    
    rtc.adjust(DateTime(year, month, day, hour, minute, second));
    rtc_update_time();
    Serial.printf("RTC time set to: %04d-%02d-%02d %02d:%02d:%02d\n", 
                  year, month, day, hour, minute, second);
}

void rtc_update_time()
{
    if (!rtcAvailable) {
        currentTime.valid = false;
        currentTime.temperature = 0.0f;
        return;
    }
    
    DateTime now = rtc.now();
    
    currentTime.hour = now.hour();
    currentTime.minute = now.minute();
    currentTime.second = now.second();
    currentTime.day = now.day();
    currentTime.month = now.month();
    currentTime.year = now.year();
    currentTime.dayOfWeek = now.dayOfTheWeek();
    currentTime.temperature = rtc.getTemperature();  // Read DS3231 temperature
    currentTime.valid = true;
}

bool rtc_is_running()
{
    return rtcAvailable && currentTime.valid;
}

float rtc_get_temperature()
{
    if (!rtcAvailable) return 0.0f;
    return rtc.getTemperature();
}

// Get formatted time string "HH:MM:SS" using DateTime structure
void rtc_get_formatted_time(char *buf, size_t bufSize)
{
    if (!rtcAvailable || !currentTime.valid) {
        snprintf(buf, bufSize, "00:00:00");
        return;
    }
    
    DateTime now = rtc.now();
    snprintf(buf, bufSize, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
}

// Get formatted date string "YYYY-MM-DD" using DateTime structure
void rtc_get_formatted_date(char *buf, size_t bufSize)
{
    if (!rtcAvailable || !currentTime.valid) {
        snprintf(buf, bufSize, "2026-01-01");  // Default date when RTC invalid
        return;
    }
    
    DateTime now = rtc.now();
    snprintf(buf, bufSize, "%04d-%02d-%02d", now.year(), now.month(), now.day());
}
