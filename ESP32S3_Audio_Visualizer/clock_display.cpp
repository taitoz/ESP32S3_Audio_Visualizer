#include "clock_display.h"
#include "rtc_time.h"
#include "pins_config.h"
#include "technics_vfd.h"

/*******************************************************************************
 * Clock Display - Technics VFD Style Implementation
 ******************************************************************************/

extern TFT_eSprite sprite;

void drawClockScreen(TFT_eSprite &spr)
{
    spr.fillSprite(TFT_BLACK);
    
    // Decorative frame border
    spr.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, VFD_GRID);
    spr.drawRect(1, 1, SCREEN_WIDTH-2, SCREEN_HEIGHT-2, VFD_GRID);
    
    // Title bar
    spr.fillRect(4, 4, SCREEN_WIDTH-8, 20, 0x0841);  // Dark blue-gray
    spr.setTextColor(VFD_CYAN_FULL, 0x0841);
    spr.setTextDatum(MC_DATUM);
    spr.drawString("DIGITAL CLOCK", SCREEN_WIDTH/2, 14, 2);
    
    if (!currentTime.valid) {
        // RTC error message
        spr.setTextColor(VFD_RED_FULL, TFT_BLACK);
        spr.setTextDatum(MC_DATUM);
        spr.drawString("RTC NOT AVAILABLE", SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 4);
        return;
    }
    
    // Large time display (HH:MM:SS)
    char timeStr[12];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", 
             currentTime.hour, currentTime.minute, currentTime.second);
    
    spr.setTextColor(VFD_CYAN_FULL, TFT_BLACK);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(timeStr, SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 10, 7);  // Font 7 = large
    
    // Date display (DD/MM/YYYY)
    char dateStr[16];
    snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", 
             currentTime.day, currentTime.month, currentTime.year);
    
    spr.setTextColor(VFD_GRID, TFT_BLACK);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(dateStr, SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 35, 4);
    
    // Day of week
    const char* daysOfWeek[] = {"Sunday", "Monday", "Tuesday", "Wednesday", 
                                 "Thursday", "Friday", "Saturday"};
    spr.setTextColor(VFD_CYAN_HALF, TFT_BLACK);
    spr.drawString(daysOfWeek[currentTime.dayOfWeek], SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 55, 2);
    
    // Temperature from RTC (bottom right corner)
    float temp = rtc_get_temperature();
    char tempStr[16];
    snprintf(tempStr, sizeof(tempStr), "%.1f°C", temp);
    spr.setTextColor(VFD_GRID, TFT_BLACK);
    spr.setTextDatum(BR_DATUM);
    spr.drawString(tempStr, SCREEN_WIDTH - 10, SCREEN_HEIGHT - 10, 2);
}

void drawTimeOverlay(TFT_eSprite &spr, int x, int y)
{
    if (!currentTime.valid) {
        spr.setTextColor(VFD_RED_HALF, TFT_BLACK);
        spr.setTextDatum(TL_DATUM);
        spr.drawString("--:--", x, y, 2);
        return;
    }
    
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", currentTime.hour, currentTime.minute);
    
    spr.setTextColor(VFD_CYAN_HALF, TFT_BLACK);
    spr.setTextDatum(TL_DATUM);
    spr.drawString(timeStr, x, y, 2);
}
