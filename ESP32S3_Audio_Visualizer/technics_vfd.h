#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pins_config.h"

/*******************************************************************************
 * Technics Authentic VFD - Compact Module
 * 
 * Two modes: EQ (SH-GE70) and VU (RS-TR373)
 * No PROGMEM images - backgrounds drawn programmatically
 * Dirty Rectangles via small SRAM sprites for 30+ FPS
 ******************************************************************************/

// ─── VFD Colors (RGB565) ────────────────────────────────────────────────────
#define VFD_CYAN_FULL     0x07FF
#define VFD_CYAN_HALF     0x03EF
#define VFD_RED_FULL      0xF800   // Bright red (5-6-5 RGB)
#define VFD_RED_HALF      0xB800   // Dim red
#define VFD_AMBER_FULL    0xFDA0   // Keep for VU mode
#define VFD_AMBER_HALF    0x7B20
#define VFD_BG            0x0000
#define VFD_GRID          0x18E3   // Dark grey for grid lines

// ─── EQ Mode (SH-GE70): 8 vertical bands ────────────────────────────────────
#define EQ_BANDS          8
#define EQ_SEG_W          36       // Segment width
#define EQ_SEG_H          8        // Segment height
#define EQ_SEG_GAP        2        // Gap between segments
#define EQ_MAX_SEGS       14       // Max segments per band
#define EQ_BAND_GAP       12       // Gap between bands
#define EQ_X0             104      // First band X
#define EQ_Y_BOTTOM       160      // Bottom of bars (grow upward)
#define EQ_SPRITE_H       ((EQ_SEG_H + EQ_SEG_GAP) * EQ_MAX_SEGS)

// ─── VU Mode (RS-TR373): 2 horizontal bars ──────────────────────────────────
#define VU_SEG_W          5        // Segment width
#define VU_SEG_H          12       // Segment height
#define VU_SEG_GAP        2        // Gap between segments
#define VU_MAX_SEGS       27       // Max segments per bar (reduced from 55)
#define VU_BAR_W          ((VU_SEG_W + VU_SEG_GAP) * VU_MAX_SEGS)
#define VU_X0             80       // Left edge of bars
#define VU_Y_L            65       // Left channel Y
#define VU_Y_R            105      // Right channel Y
#define VU_0DB_SEG        22       // 0dB mark at segment 22 (~80% of 27)

// ─── Peak Hold ──────────────────────────────────────────────────────────────
#define PEAK_HOLD_MS      500
#define PEAK_FADE_MS      300

// ─── Public Interface ───────────────────────────────────────────────────────
void technics_vfd_init(TFT_eSPI &tft);
void technics_vfd_draw_eq(TFT_eSPI &tft, const float *bands, int numBands);
void technics_vfd_draw_vu(TFT_eSPI &tft, float rmsL, float rmsR);
void technics_vfd_draw_bg_eq(TFT_eSPI &tft);
void technics_vfd_draw_bg_vu(TFT_eSPI &tft);
void technics_vfd_reset_state();
