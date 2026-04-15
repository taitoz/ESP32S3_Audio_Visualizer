#include "rtc_time.h"
#include "pins_config.h"
#include <Wire.h>

/*******************************************************************************
 * RTC DS3231 Implementation
 ******************************************************************************/

volatile RTCTime currentTime = {0, 0, 0, 1, 1, 2026, 0, false};

static RTC_DS3231 rtc;
static TwoWire rtcWire = TwoWire(1);  // Use I2C1 for RTC (separate from touch I2C0)
static bool rtcAvailable = false;     // Track if RTC is present

void rtc_init()
{
    // Initialize I2C for RTC on dedicated bus
    rtcWire.begin(RTC_I2C_SDA, RTC_I2C_SCL, RTC_I2C_FREQ);
    Serial.printf("RTC I2C initialized: SDA=%d, SCL=%d\n", RTC_I2C_SDA, RTC_I2C_SCL);
    
    if (!rtc.begin(&rtcWire)) {
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
