#pragma once

#include <TFT_eSPI.h>

/*******************************************************************************
 * Clock Display - Technics VFD Style
 * 
 * Displays current time in vintage Technics VFD aesthetic.
 * Uses cyan color (#00FFFF) for authentic VFD look.
 ******************************************************************************/

// Draw clock screen (full frame, caller pushes to display)
void drawClockScreen(TFT_eSprite &sprite);

// Draw compact time overlay (for status bar)
void drawTimeOverlay(TFT_eSprite &sprite, int x, int y);
