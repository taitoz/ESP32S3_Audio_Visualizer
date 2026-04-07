#include "technics_vfd.h"
#include "AXS15231B.h"
#include <TJpg_Decoder.h>

/*******************************************************************************
 * Technics Authentic VFD - Implementation
 * 
 * IMPORTANT: AXS15231B display uses custom QSPI driver.
 * TFT_eSPI pushSprite/fillRect/drawString do NOT reach the screen.
 * All rendering must go through the main PSRAM sprite → lcd_PushColors_rotated_90.
 * 
 * Dirty Rectangles: draw changed bands/bars into sprite, push only the
 * dirty horizontal stripe via lcd_PushColors_rotated_90.
 ******************************************************************************/

// ─── External: main sprite from .ino ────────────────────────────────────────
extern TFT_eSprite sprite;

// ─── EQ State ───────────────────────────────────────────────────────────────
static float eq_filtered[EQ_BANDS] = {0};
static int   eq_last_full[EQ_BANDS] = {0};
static int   eq_last_half[EQ_BANDS] = {0};

// ─── VU State ───────────────────────────────────────────────────────────────
static float vu_filtered[2] = {0};
static int   vu_last_seg[2] = {0};
static int   vu_peak_seg[2] = {0};
static unsigned long vu_peak_time[2] = {0};

static bool inited = false;

// VU-only sprites for performance (no full-frame push)
static TFT_eSprite *vuSpriteL = nullptr;
static TFT_eSprite *vuSpriteR = nullptr;

// ─── Helpers ────────────────────────────────────────────────────────────────

// EQ color mapping: cyan (low) → red (high)
static inline uint16_t vfd_color_eq(int seg, int threshold, bool half) {
    if (seg >= threshold) return half ? VFD_RED_HALF : VFD_RED_FULL;
    return half ? VFD_CYAN_HALF : VFD_CYAN_FULL;
}

// VU color mapping: amber for peak zone (>= 0dB), cyan for normal levels
static inline uint16_t vfd_color_vu(int seg, int threshold, bool half) {
    if (seg >= VU_0DB_SEG) {  // Peak zone (+3, +6, +8 dB)
        return half ? VFD_AMBER_HALF : VFD_AMBER_FULL;
    }
    return half ? VFD_CYAN_HALF : VFD_CYAN_FULL;
}

// ─── Draw Programmatic EQ Background into sprite ────────────────────────────
void technics_vfd_draw_bg_eq(TFT_eSPI &tft) {
    sprite.fillSprite(TFT_BLACK);  // ОБЯЗАТЕЛЬНО: очищаем весь буфер кадра

    // Vertical guide lines on sides of each band
    // for (int b = 0; b < EQ_BANDS; b++) {
    //     int x = EQ_X0 + b * (EQ_SEG_W + EQ_BAND_GAP);
    //     sprite.drawFastVLine(x - 1, EQ_Y_BOTTOM - EQ_SPRITE_H, EQ_SPRITE_H, VFD_GRID);
    //     sprite.drawFastVLine(x + EQ_SEG_W, EQ_Y_BOTTOM - EQ_SPRITE_H, EQ_SPRITE_H, VFD_GRID);
    // }

    // Horizontal 0dB line at ~80% height
    // int zeroY = EQ_Y_BOTTOM - (int)(EQ_SPRITE_H * 0.8f);
    // sprite.drawFastHLine(EQ_X0 - 5, zeroY, EQ_BANDS * (EQ_SEG_W + EQ_BAND_GAP), VFD_GRID);

    // Frequency labels - now embedded in JPG background, no text drawing needed
    // If needed, can use built-in font: sprite.drawString("63", cx, EQ_Y_BOTTOM + 4, 1);

    // TODO: Load JPG background here
    // sprite.pushImage(x, y, width, height, image_data);
    // or tft.pushImage for direct to display

    // No push here — caller pushes full frame
}

// ─── Draw Programmatic VU Background into sprite ────────────────────────────
void technics_vfd_draw_bg_vu(TFT_eSPI &tft) {
    sprite.fillSprite(TFT_BLACK);  // ОБЯЗАТЕЛЬНО: очищаем весь буфер кадра

    // Channel labels and dB scale - now embedded in JPG background
    // Grid lines only
    // sprite.setTextColor(VFD_GRID, VFD_BG);
    // const int db_segs[] = {0, 2, 4, VU_0DB_SEG, 7, VU_MAX_SEGS - 1};
    // for (int i = 0; i < 6; i++) {
    //     int x = VU_X0 + db_segs[i] * (VU_SEG_W + VU_SEG_GAP);
    //     sprite.drawFastVLine(x, VU_Y_L - 8, 5, VFD_GRID);
    //     sprite.drawFastVLine(x, VU_Y_R + VU_SEG_H + 3, 5, VFD_GRID);
    // }

    // Horizontal guide lines
    // sprite.drawFastHLine(VU_X0, VU_Y_L - 1, VU_BAR_W, VFD_GRID);
    // sprite.drawFastHLine(VU_X0, VU_Y_L + VU_SEG_H + 1, VU_BAR_W, VFD_GRID);
    // sprite.drawFastHLine(VU_X0, VU_Y_R - 1, VU_BAR_W, VFD_GRID);
    // sprite.drawFastHLine(VU_X0, VU_Y_R + VU_SEG_H + 1, VU_BAR_W, VFD_GRID);

    // TODO: Load JPG background here
    // sprite.pushImage(x, y, width, height, image_data);
    // or tft.pushImage for direct to display

    // No push here — caller pushes full frame
}

// ─── Init ───────────────────────────────────────────────────────────────────
void technics_vfd_init(TFT_eSPI &tft) {
    if (inited) return;

    // VU sprites no longer needed - we draw directly in main sprite
    
    for (int i = 0; i < EQ_BANDS; i++) {
        eq_filtered[i] = 0;
        eq_last_full[i] = -1;
        eq_last_half[i] = -1;
    }
    for (int i = 0; i < 2; i++) {
        vu_filtered[i] = 0;
        vu_last_seg[i] = -1;
        vu_peak_seg[i] = 0;
        vu_peak_time[i] = 0;
    }

    inited = true;
    Serial.printf("Technics VFD init OK. Free heap: %u\n", ESP.getFreeHeap());
}

// ─── Reset state (call on mode switch to avoid stale dirty-check data) ──────
void technics_vfd_reset_state() {
    for (int i = 0; i < EQ_BANDS; i++) {
        eq_last_full[i] = -1;
        eq_last_half[i] = -1;
    }
    for (int i = 0; i < 2; i++) {
        vu_last_seg[i] = -1;
        vu_peak_seg[i] = 0;
        vu_peak_time[i] = 0;
    }
}

// ─── EQ Update (Dirty Rectangles — draw into sprite, push dirty stripes) ───
void technics_vfd_draw_eq(TFT_eSPI &tft, const float *bands, int numBands) {
    if (!inited) return;

    bool any_dirty = false;

    for (int b = 0; b < EQ_BANDS; b++) {
        // Map 32 FFT bands → 8 EQ bands by averaging groups of 4
        float val = 0;
        int start = b * numBands / EQ_BANDS;
        int end   = (b + 1) * numBands / EQ_BANDS;
        if (end <= start) end = start + 1;
        float sum = 0;
        int cnt = 0;
        for (int j = start; j < end && j < numBands; j++) {
            sum += bands[j];
            cnt++;
        }
        val = cnt > 0 ? sum / cnt : 0;

        // Clamp 0..1
        if (val < 0) val = 0;
        if (val > 1.0f) val = 1.0f;

        // EMA smoothing: fast attack, slow release
        if (val > eq_filtered[b])
            eq_filtered[b] = 0.7f * val + 0.3f * eq_filtered[b];
        else
            eq_filtered[b] = 0.15f * val + 0.85f * eq_filtered[b];

        // Compute segments: full + half
        float seg_f = eq_filtered[b] * EQ_MAX_SEGS;
        int full = (int)seg_f;
        float frac = seg_f - full;
        int half = 0;
        if (frac > 0.75f) { full++; half = 0; }
        else if (frac > 0.25f) { half = 1; }
        if (full > EQ_MAX_SEGS) full = EQ_MAX_SEGS;

        // Dirty check
        if (full == eq_last_full[b] && half == eq_last_half[b]) continue;
        eq_last_full[b] = full;
        eq_last_half[b] = half;
        any_dirty = true;

        // Draw this band into sprite
        int bx = EQ_X0 + b * (EQ_SEG_W + EQ_BAND_GAP);
        int by_top = EQ_Y_BOTTOM - EQ_SPRITE_H;

        for (int seg = 0; seg < EQ_MAX_SEGS; seg++) {
            int sy = EQ_Y_BOTTOM - (seg + 1) * (EQ_SEG_H + EQ_SEG_GAP);
            if (sy < by_top) break;

            if (seg < full) {
                int threshold = (int)(EQ_MAX_SEGS * 0.8f);
                sprite.fillRect(bx, sy, EQ_SEG_W, EQ_SEG_H, vfd_color_eq(seg, threshold, false));
            } else if (seg == full && half) {
                int threshold = (int)(EQ_MAX_SEGS * 0.8f);
                sprite.fillRect(bx, sy, EQ_SEG_W, EQ_SEG_H, vfd_color_eq(seg, threshold, true));
            } else {
                sprite.fillRect(bx, sy, EQ_SEG_W, EQ_SEG_H, VFD_BG);
            }
        }
    }

    // No push here — caller pushes full frame after FPS overlay
}

// ─── VU Update (Draw directly in main sprite) ───────────────────────────────
void technics_vfd_draw_vu(TFT_eSPI &tft, float rmsL, float rmsR) {
    if (!inited) return;
    
    unsigned long now = millis();
    float rms_in[2] = {rmsL, rmsR};
    int   y_pos[2]  = {VU_Y_L, VU_Y_R};

    // Clear VU areas in main sprite first
    sprite.fillRect(VU_X0, VU_Y_L, VU_BAR_W, VU_SEG_H, TFT_BLACK);
    sprite.fillRect(VU_X0, VU_Y_R, VU_BAR_W, VU_SEG_H, TFT_BLACK);

    for (int ch = 0; ch < 2; ch++) {
        float val = rms_in[ch];
        if (val < 0) val = 0;
        if (val > 1.0f) val = 1.0f;

        // EMA: instant attack, viscous release
        if (val > vu_filtered[ch])
            vu_filtered[ch] = 0.9f * val + 0.1f * vu_filtered[ch];
        else
            vu_filtered[ch] = 0.15f * val + 0.85f * vu_filtered[ch];

        int lit = (int)(vu_filtered[ch] * VU_MAX_SEGS);
        if (lit > VU_MAX_SEGS) lit = VU_MAX_SEGS;

        // Peak hold
        if (lit > vu_peak_seg[ch]) {
            vu_peak_seg[ch] = lit;
            vu_peak_time[ch] = now;
        }
        int peak_seg = vu_peak_seg[ch];
        bool peak_active = (now - vu_peak_time[ch]) < PEAK_HOLD_MS;
        bool peak_fading = !peak_active && (now - vu_peak_time[ch]) < (PEAK_HOLD_MS + PEAK_FADE_MS);
        if (!peak_active && !peak_fading) {
            vu_peak_seg[ch] = lit;
        }

        // Draw VU bar directly in main sprite
        int bar_y = y_pos[ch];
        
        // Update last segment tracking
        vu_last_seg[ch] = lit;
        
        for (int seg = 0; seg < VU_MAX_SEGS; seg++) {
            int sx = VU_X0 + seg * (VU_SEG_W + VU_SEG_GAP);

            if (seg < lit) {
                sprite.fillRect(sx, bar_y, VU_SEG_W, VU_SEG_H, vfd_color_vu(seg, VU_0DB_SEG, false));
            } else if (seg == lit) {
                float frac = vu_filtered[ch] * VU_MAX_SEGS - lit;
                if (frac > 0.25f) {
                    bool is_full = frac > 0.75f;
                    sprite.fillRect(sx, bar_y, VU_SEG_W, VU_SEG_H, vfd_color_vu(seg, VU_0DB_SEG, !is_full));
                }
            }
        }

        // Peak hold dot
        if ((peak_active || peak_fading) && peak_seg > 0 && peak_seg < VU_MAX_SEGS) {
            int px = VU_X0 + peak_seg * (VU_SEG_W + VU_SEG_GAP);
            bool half = peak_fading;
            sprite.fillRect(px, bar_y, VU_SEG_W, VU_SEG_H, vfd_color_vu(peak_seg, VU_0DB_SEG, half));
        }
    }
    // No push here - VU bars are 592px wide (almost full screen)
    // Partial update doesn't make sense, caller will do full frame push
}
