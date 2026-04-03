#include "spectrum.h"
#include <ArduinoFFT.h>

/*******************************************************************************
 * Minimal Spectrum Module - FFT Only Implementation
 * 
 * Provides FFT computation for Technics EQ mode.
 * All visualization code removed - only FFT processing remains.
 ******************************************************************************/

// ─── External Variables (from audio_sampling) ───────────────────────────────
extern float vRealL[SAMPLES];
extern float vImagL[SAMPLES];
extern float vRealR[SAMPLES];
extern float vImagR[SAMPLES];

// ─── Band Values (FFT output) ───────────────────────────────────────────────
float bandValuesL[NUM_BANDS] = {0};
float bandValuesR[NUM_BANDS] = {0};

// ─── FFT Objects ───────────────────────────────────────────────────────────
static ArduinoFFT<float> FFT_L(vRealL, vImagL, SAMPLES, SAMPLING_FREQ);
static ArduinoFFT<float> FFT_R(vRealR, vImagR, SAMPLES, SAMPLING_FREQ);

// ─── Public Functions ───────────────────────────────────────────────────────

void spectrum_init(void) {
    // Initialize band arrays
    for (int i = 0; i < NUM_BANDS; i++) {
        bandValuesL[i] = 0;
        bandValuesR[i] = 0;
    }
    
    Serial.println("Minimal spectrum FFT initialized");
}

void spectrum_compute_fft(void) {
    // Left channel FFT
    FFT_L.windowing(vRealL, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT_L.compute(vRealL, vImagL, SAMPLES, FFT_FORWARD);
    FFT_L.complexToMagnitude(vRealL, vImagL, SAMPLES);
    
    // Right channel FFT
    FFT_R.windowing(vRealR, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT_R.compute(vRealR, vImagR, SAMPLES, FFT_FORWARD);
    FFT_R.complexToMagnitude(vRealR, vImagR, SAMPLES);
    
    // Convert FFT bins to frequency bands (simplified)
    int halfSamples = SAMPLES / 2;
    for (int i = 0; i < NUM_BANDS; i++) {
        int startBin = i * halfSamples / NUM_BANDS;
        int endBin = (i + 1) * halfSamples / NUM_BANDS;
        
        float sumL = 0, sumR = 0;
        int count = 0;
        
        for (int bin = startBin; bin < endBin && bin < halfSamples; bin++) {
            sumL += vRealL[bin];
            sumR += vRealR[bin];
            count++;
        }
        
        if (count > 0) {
            bandValuesL[i] = sumL / count;
            bandValuesR[i] = sumR / count;
        }
    }
}
