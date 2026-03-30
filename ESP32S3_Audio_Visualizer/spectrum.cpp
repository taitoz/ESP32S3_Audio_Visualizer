#include "spectrum.h"
#include <arduinoFFT.h>
#include "pins_config.h"

/*******************************************************************************
 * FFT processing and bar-style spectrum visualization
 ******************************************************************************/

float bandValues[NUM_BANDS]    = {0};
float peakValues[NUM_BANDS]    = {0};
int   peakHoldCount[NUM_BANDS] = {0};

static float bandSmoothed[NUM_BANDS] = {0};

// ArduinoFFT object — operates on the global vReal/vImag arrays
static ArduinoFFT<double> FFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

void spectrum_init()
{
    for (int i = 0; i < NUM_BANDS; i++) {
        bandValues[i]    = 0;
        bandSmoothed[i]  = 0;
        peakValues[i]    = 0;
        peakHoldCount[i] = 0;
    }
}

void spectrum_compute_fft()
{
    // Apply Hamming window, then FFT, then compute magnitudes
    FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(FFT_FORWARD);
    FFT.complexToMagnitude();

    // Map FFT bins to NUM_BANDS using logarithmic frequency distribution
    // Frequency per bin = SAMPLING_FREQ / SAMPLES ≈ 21.5 Hz per bin
    // Usable bins: 1 .. SAMPLES/2-1  (skip DC bin 0)
    float newBands[NUM_BANDS] = {0};
    int   bandCounts[NUM_BANDS] = {0};

    int maxBin = SAMPLES / 2;

    for (int i = 1; i < maxBin; i++) {
        // Log-scale mapping: bin i → band index
        float freq = (float)i * SAMPLING_FREQ / SAMPLES;
        float minFreq = 30.0f;
        float maxFreq = (float)(SAMPLING_FREQ / 2);

        if (freq < minFreq) continue;
        if (freq > maxFreq) break;

        // Logarithmic mapping
        float logMin = log10f(minFreq);
        float logMax = log10f(maxFreq);
        float logF   = log10f(freq);
        int band = (int)((logF - logMin) / (logMax - logMin) * NUM_BANDS);
        if (band < 0) band = 0;
        if (band >= NUM_BANDS) band = NUM_BANDS - 1;

        newBands[band] += (float)vReal[i];
        bandCounts[band]++;
    }

    // Average and smooth
    for (int i = 0; i < NUM_BANDS; i++) {
        float val = 0;
        if (bandCounts[i] > 0) {
            val = newBands[i] / bandCounts[i];
        }

        // Scale to displayable range (0–SCREEN_HEIGHT)
        // Adjust the divisor to calibrate sensitivity for your input signal
        val = val / 300.0f;
        if (val > SCREEN_HEIGHT) val = SCREEN_HEIGHT;

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

// Color gradient: green → yellow → red based on height
static uint16_t barColor(int y, int maxH)
{
    float ratio = (float)y / (float)maxH;
    if (ratio < 0.5f) {
        // Green to Yellow
        uint8_t r = (uint8_t)(ratio * 2.0f * 255);
        uint8_t g = 255;
        uint8_t b = 0;
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    } else {
        // Yellow to Red
        uint8_t r = 255;
        uint8_t g = (uint8_t)((1.0f - (ratio - 0.5f) * 2.0f) * 255);
        uint8_t b = 0;
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
}

void spectrum_draw_bars(TFT_eSprite &spr)
{
    int barWidth = (SCREEN_WIDTH - 2) / NUM_BANDS;
    int gap = 2;  // pixels between bars
    int effectiveBar = barWidth - gap;
    if (effectiveBar < 1) effectiveBar = 1;

    int startX = (SCREEN_WIDTH - (barWidth * NUM_BANDS)) / 2;

    for (int i = 0; i < NUM_BANDS; i++) {
        int x = startX + i * barWidth;
        int barH = (int)bandValues[i];
        if (barH < 0) barH = 0;
        if (barH > SCREEN_HEIGHT) barH = SCREEN_HEIGHT;

        // Draw filled bar from bottom up with gradient
        for (int y = 0; y < barH; y++) {
            uint16_t col = barColor(y, SCREEN_HEIGHT);
            spr.drawFastHLine(x, SCREEN_HEIGHT - 1 - y, effectiveBar, col);
        }

        // Draw peak dot
        int peakY = (int)peakValues[i];
        if (peakY > 0 && peakY < SCREEN_HEIGHT) {
            spr.drawFastHLine(x, SCREEN_HEIGHT - 1 - peakY, effectiveBar, TFT_WHITE);
        }
    }
}
