#include "spectrum.h"
#include <arduinoFFT.h>
#include "pins_config.h"

/*******************************************************************************
 * FFT processing and bar-style spectrum visualization (Stereo)
 * 
 * Layout: L channel bars grow DOWN from center, R channel bars grow UP from center
 * This creates a mirrored stereo spectrum display across the 640px width.
 ******************************************************************************/

float bandValuesL[NUM_BANDS]    = {0};
float peakValuesL[NUM_BANDS]    = {0};
float bandValuesR[NUM_BANDS]    = {0};
float peakValuesR[NUM_BANDS]    = {0};

static int   peakHoldCountL[NUM_BANDS] = {0};
static int   peakHoldCountR[NUM_BANDS] = {0};
static float bandSmoothedL[NUM_BANDS]  = {0};
static float bandSmoothedR[NUM_BANDS]  = {0};

// Two ArduinoFFT objects — one per channel
static ArduinoFFT<double> FFT_L(vRealL, vImagL, SAMPLES, SAMPLING_FREQ);
static ArduinoFFT<double> FFT_R(vRealR, vImagR, SAMPLES, SAMPLING_FREQ);

void spectrum_init()
{
    for (int i = 0; i < NUM_BANDS; i++) {
        bandValuesL[i]    = 0;  bandValuesR[i]    = 0;
        bandSmoothedL[i]  = 0;  bandSmoothedR[i]  = 0;
        peakValuesL[i]    = 0;  peakValuesR[i]    = 0;
        peakHoldCountL[i] = 0;  peakHoldCountR[i] = 0;
    }
}

// Process one channel's FFT bins into band values
static void process_bands(double *vReal, float *bandValues, float *bandSmoothed,
                          float *peakValues, int *peakHoldCount, int halfH)
{
    float newBands[NUM_BANDS] = {0};
    int   bandCounts[NUM_BANDS] = {0};
    int   maxBin = SAMPLES / 2;

    for (int i = 1; i < maxBin; i++) {
        float freq = (float)i * SAMPLING_FREQ / SAMPLES;
        float minFreq = 30.0f;
        float maxFreq = (float)(SAMPLING_FREQ / 2);

        if (freq < minFreq) continue;
        if (freq > maxFreq) break;

        float logMin = log10f(minFreq);
        float logMax = log10f(maxFreq);
        float logF   = log10f(freq);
        int band = (int)((logF - logMin) / (logMax - logMin) * NUM_BANDS);
        if (band < 0) band = 0;
        if (band >= NUM_BANDS) band = NUM_BANDS - 1;

        newBands[band] += (float)vReal[i];
        bandCounts[band]++;
    }

    for (int i = 0; i < NUM_BANDS; i++) {
        float val = 0;
        if (bandCounts[i] > 0) {
            val = newBands[i] / bandCounts[i];
        }

        // Scale to half-screen height (each channel gets half)
        // Adjust the divisor to calibrate sensitivity for your input signal
        val = val / 300.0f;
        if (val > halfH) val = halfH;

        // Exponential smoothing
        bandSmoothed[i] = bandSmoothed[i] * BAND_SMOOTHING + val * (1.0f - BAND_SMOOTHING);
        bandValues[i] = bandSmoothed[i];

        // Peak detection with hold & fall
        if (bandValues[i] > peakValues[i]) {
            peakValues[i] = bandValues[i];
            peakHoldCount[i] = PEAK_HOLD_FRAMES;
        } else {
            if (peakHoldCount[i] > 0) {
                peakHoldCount[i]--;
            } else {
                peakValues[i] -= PEAK_FALL_RATE;
                if (peakValues[i] < 0) peakValues[i] = 0;
            }
        }
    }
}

void spectrum_compute_fft()
{
    int halfH = SCREEN_HEIGHT / 2;

    // Left channel FFT
    FFT_L.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT_L.compute(FFT_FORWARD);
    FFT_L.complexToMagnitude();
    process_bands(vRealL, bandValuesL, bandSmoothedL, peakValuesL, peakHoldCountL, halfH);

    // Right channel FFT
    FFT_R.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT_R.compute(FFT_FORWARD);
    FFT_R.complexToMagnitude();
    process_bands(vRealR, bandValuesR, bandSmoothedR, peakValuesR, peakHoldCountR, halfH);
}

// Color gradient: green → yellow → red based on height ratio
static uint16_t barColor(int y, int maxH)
{
    float ratio = (float)y / (float)maxH;
    if (ratio < 0.5f) {
        uint8_t r = (uint8_t)(ratio * 2.0f * 255);
        uint8_t g = 255;
        uint8_t b = 0;
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    } else {
        uint8_t r = 255;
        uint8_t g = (uint8_t)((1.0f - (ratio - 0.5f) * 2.0f) * 255);
        uint8_t b = 0;
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
}

void spectrum_draw_bars(TFT_eSprite &spr)
{
    int centerY = SCREEN_HEIGHT / 2;
    int halfH   = SCREEN_HEIGHT / 2;
    int barWidth = (SCREEN_WIDTH - 2) / NUM_BANDS;
    int gap = 2;
    int effectiveBar = barWidth - gap;
    if (effectiveBar < 1) effectiveBar = 1;
    int startX = (SCREEN_WIDTH - (barWidth * NUM_BANDS)) / 2;

    // Center line
    spr.drawFastHLine(0, centerY, SCREEN_WIDTH, 0x2945);

    for (int i = 0; i < NUM_BANDS; i++) {
        int x = startX + i * barWidth;

        // ── Left channel: bars grow UPWARD from center ──
        int barHL = (int)bandValuesL[i];
        if (barHL < 0) barHL = 0;
        if (barHL > halfH) barHL = halfH;

        for (int y = 0; y < barHL; y++) {
            uint16_t col = barColor(y, halfH);
            spr.drawFastHLine(x, centerY - 1 - y, effectiveBar, col);
        }

        int peakYL = (int)peakValuesL[i];
        if (peakYL > 0 && peakYL < halfH) {
            spr.drawFastHLine(x, centerY - 1 - peakYL, effectiveBar, TFT_WHITE);
        }

        // ── Right channel: bars grow DOWNWARD from center ──
        int barHR = (int)bandValuesR[i];
        if (barHR < 0) barHR = 0;
        if (barHR > halfH) barHR = halfH;

        for (int y = 0; y < barHR; y++) {
            uint16_t col = barColor(y, halfH);
            spr.drawFastHLine(x, centerY + 1 + y, effectiveBar, col);
        }

        int peakYR = (int)peakValuesR[i];
        if (peakYR > 0 && peakYR < halfH) {
            spr.drawFastHLine(x, centerY + 1 + peakYR, effectiveBar, TFT_WHITE);
        }
    }

    // Channel labels
    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    spr.setTextDatum(TL_DATUM);
    spr.drawString("L", 2, 2, 1);
    spr.setTextDatum(BL_DATUM);
    spr.drawString("R", 2, SCREEN_HEIGHT - 2, 1);
}
