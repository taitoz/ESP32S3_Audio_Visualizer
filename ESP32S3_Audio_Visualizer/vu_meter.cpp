#include "vu_meter.h"
#include <math.h>

/*******************************************************************************
 * VU Meter — Stereo, multiple visualization styles with ballistic smoothing
 ******************************************************************************/

static float smoothedRMS[2]  = {0.0f, 0.0f};
static float smoothedPeak[2] = {0.0f, 0.0f};
static float currentDB[2]    = {-60.0f, -60.0f};
static float peakDB[2]       = {-60.0f, -60.0f};

// Convert linear amplitude to dB (referenced to ADC_CENTER)
static float toDB(float amplitude)
{
    if (amplitude < 1.0f) amplitude = 1.0f;
    // Reference: ADC_CENTER (2048) = 0 dB full scale
    return 20.0f * log10f(amplitude / 2048.0f);
}

// Apply VU ballistics to one channel
static void update_channel(int ch, float rms, float peak)
{
    if (rms > smoothedRMS[ch]) {
        smoothedRMS[ch] = smoothedRMS[ch] + VU_ATTACK_COEFF * (rms - smoothedRMS[ch]);
    } else {
        smoothedRMS[ch] = smoothedRMS[ch] + VU_RELEASE_COEFF * (rms - smoothedRMS[ch]);
    }

    if (peak > smoothedPeak[ch]) {
        smoothedPeak[ch] = peak;
    } else {
        smoothedPeak[ch] = smoothedPeak[ch] * 0.6f;
    }

    currentDB[ch] = toDB(smoothedRMS[ch]);
    peakDB[ch]    = toDB(smoothedPeak[ch]);

    if (currentDB[ch] < -60.0f) currentDB[ch] = -60.0f;
    if (currentDB[ch] > 3.0f)   currentDB[ch] = 3.0f;
    if (peakDB[ch] < -60.0f)    peakDB[ch] = -60.0f;
    if (peakDB[ch] > 3.0f)      peakDB[ch] = 3.0f;
}

void vu_meter_init()
{
    for (int ch = 0; ch < 2; ch++) {
        smoothedRMS[ch]  = 0.0f;
        smoothedPeak[ch] = 0.0f;
        currentDB[ch]    = -60.0f;
        peakDB[ch]       = -60.0f;
    }
}

void vu_meter_update(float rmsL, float peakL, float rmsR, float peakR)
{
    update_channel(CH_LEFT,  rmsL, peakL);
    update_channel(CH_RIGHT, rmsR, peakR);
}

float vu_get_db(int ch)      { return currentDB[ch]; }
float vu_get_peak_db(int ch) { return peakDB[ch]; }

// ─── Helper: map dB to 0.0–1.0 range ────────────────────────────────────────
static float dbToNorm(float db)
{
    // Map -60..+3 dB → 0..1
    float n = (db + 60.0f) / 63.0f;
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    return n;
}

// ─── Helper: RGB565 from R,G,B ──────────────────────────────────────────────
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

// ─── STYLE 1: Classic Needle VU Meter ───────────────────────────────────────
void vu_meter_draw_needle(TFT_eSprite &spr)
{
    // Draw two meters side by side (L and R — true stereo)
    int meterW = SCREEN_WIDTH / 2 - 20;
    int meterH = SCREEN_HEIGHT - 20;
    int cx1 = SCREEN_WIDTH / 4;
    int cx2 = 3 * SCREEN_WIDTH / 4;
    int cy  = SCREEN_HEIGHT - 10;  // needle pivot near bottom
    int radius = meterH - 10;

    // Background arcs
    for (int m = 0; m < 2; m++) {
        int cx = (m == 0) ? cx1 : cx2;

        // Draw meter face (dark arc background)
        spr.fillRect(cx - meterW/2, 5, meterW, meterH, rgb565(20, 20, 30));
        spr.drawRect(cx - meterW/2, 5, meterW, meterH, rgb565(60, 60, 80));

        // Draw scale arc markings
        for (int tick = 0; tick <= 20; tick++) {
            float angle = PI * 0.2f + (float)tick / 20.0f * PI * 0.6f;
            float db_at_tick = -60.0f + tick * 3.15f;

            int x1 = cx + (int)(cosf(PI - angle) * (radius - 5));
            int y1 = cy - (int)(sinf(PI - angle) * (radius - 5));
            int x2 = cx + (int)(cosf(PI - angle) * (radius - ((tick % 5 == 0) ? 18 : 10)));
            int y2 = cy - (int)(sinf(PI - angle) * (radius - ((tick % 5 == 0) ? 18 : 10)));

            uint16_t tickColor = (db_at_tick > -3.0f) ? TFT_RED : rgb565(180, 180, 180);
            spr.drawLine(x1, y1, x2, y2, tickColor);
        }

        // Draw dB labels
        spr.setTextColor(rgb565(180, 180, 180), rgb565(20, 20, 30));
        spr.setTextDatum(MC_DATUM);

        const char* labels[] = {"-40", "-20", "-10", "-5", "0", "+3"};
        float labelDB[] = {-40, -20, -10, -5, 0, 3};
        for (int l = 0; l < 6; l++) {
            float norm = dbToNorm(labelDB[l]);
            float angle = PI * 0.2f + norm * PI * 0.6f;
            int lx = cx + (int)(cosf(PI - angle) * (radius - 28));
            int ly = cy - (int)(sinf(PI - angle) * (radius - 28));
            spr.drawString(labels[l], lx, ly, 1);
        }

        // Draw needle — use this meter's channel dB
        float chDB = currentDB[m];
        float norm = dbToNorm(chDB);
        float angle = PI * 0.2f + norm * PI * 0.6f;
        int nx = cx + (int)(cosf(PI - angle) * (radius - 2));
        int ny = cy - (int)(sinf(PI - angle) * (radius - 2));
        spr.drawLine(cx, cy, nx, ny, TFT_WHITE);
        spr.drawLine(cx-1, cy, nx-1, ny, TFT_WHITE);

        // Pivot dot
        spr.fillCircle(cx, cy, 4, TFT_RED);
        spr.fillCircle(cx, cy, 2, TFT_WHITE);
    }

    // Title
    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.drawString("VU METER - NEEDLE", SCREEN_WIDTH / 2, 2, 1);
}

// ─── STYLE 2: LED Ladder (segmented bar) ───────────────────────────────────
void vu_meter_draw_led_ladder(TFT_eSprite &spr)
{
    int numSegs     = 40;       // number of LED segments
    int segW        = (SCREEN_WIDTH - 40) / numSegs;
    int segH        = 50;
    int gap         = 2;
    int startX      = 20;
    int barY1       = 30;       // top bar (Left channel)
    int barY2       = 100;      // bottom bar (Right channel)

    float normL  = dbToNorm(currentDB[CH_LEFT]);
    float normR  = dbToNorm(currentDB[CH_RIGHT]);
    int litL  = (int)(normL * numSegs);
    int litR  = (int)(normR * numSegs);

    // Labels
    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    spr.setTextDatum(TL_DATUM);
    spr.drawString("L", 2, barY1 + 15, 2);

    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawString("R", 2, barY2 + 15, 2);

    for (int i = 0; i < numSegs; i++) {
        int x = startX + i * segW;
        float ratio = (float)i / (float)numSegs;

        // Color: green → yellow → red
        uint16_t onColor;
        if (ratio < 0.5f) {
            onColor = rgb565(0, 255 - (uint8_t)(ratio * 2.0f * 100), 0);
        } else if (ratio < 0.8f) {
            onColor = rgb565(255, 200 - (uint8_t)((ratio - 0.5f) * 3.3f * 200), 0);
        } else {
            onColor = rgb565(255, 0, 0);
        }

        uint16_t offColor = rgb565(20, 20, 20);

        // Left channel bar
        uint16_t col = (i < litL) ? onColor : offColor;
        spr.fillRect(x, barY1, segW - gap, segH, col);

        // Right channel bar
        col = (i < litR) ? onColor : offColor;
        spr.fillRect(x, barY2, segW - gap, segH, col);
    }

    // dB readout
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setTextDatum(TR_DATUM);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f dB", currentDB[CH_LEFT]);
    spr.drawString(buf, SCREEN_WIDTH - 5, barY1 + 15, 2);
    snprintf(buf, sizeof(buf), "%.1f dB", currentDB[CH_RIGHT]);
    spr.drawString(buf, SCREEN_WIDTH - 5, barY2 + 15, 2);

    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.drawString("VU METER - LED LADDER", SCREEN_WIDTH / 2, 2, 1);
}

// ─── Dispatch ───────────────────────────────────────────────────────────────
void vu_meter_draw(TFT_eSprite &spr, VUStyle style)
{
    switch (style) {
        case VU_STYLE_NEEDLE:     vu_meter_draw_needle(spr);     break;
        case VU_STYLE_LED_LADDER: vu_meter_draw_led_ladder(spr); break;
        default:                  vu_meter_draw_needle(spr);      break;
    }
}
