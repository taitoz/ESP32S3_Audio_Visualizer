#include "spectrum.h"
#include <arduinoFFT.h>

/*******************************************************************************
 * Minimal Spectrum Module - ArduinoFFT 2.0.4 Implementation
 * 
 * Provides FFT computation for Technics EQ mode using ArduinoFFT library.
 * All visualization code removed - only FFT processing remains.
 ******************************************************************************/

// ─── Band Values (FFT output) ───────────────────────────────────────────────
float bandValuesL[NUM_BANDS] = {0};
float bandValuesR[NUM_BANDS] = {0};

// ─── FFT Objects (ArduinoFFT 2.0.4 API) ───────────────────────────────────────
static ArduinoFFT<float> FFT = ArduinoFFT<float>();

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
    // Left channel FFT - ArduinoFFT 2.0.4 API
    FFT.windowing(vRealL, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(vRealL, vImagL, SAMPLES, FFT_FORWARD);
    FFT.complexToMagnitude(vRealL, vImagL, SAMPLES);
    
    // Right channel FFT - ArduinoFFT 2.0.4 API
    FFT.windowing(vRealR, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(vRealR, vImagR, SAMPLES, FFT_FORWARD);
    FFT.complexToMagnitude(vRealR, vImagR, SAMPLES);
    
    // Convert FFT bins to frequency bands (normalized 0..1)
    // Use logarithmic band mapping for perceptually even distribution
    int halfSamples = SAMPLES / 2;
    
    for (int i = 0; i < NUM_BANDS; i++) {
        // Linear bin mapping
        int startBin = i * halfSamples / NUM_BANDS;
        int endBin = (i + 1) * halfSamples / NUM_BANDS;
        if (startBin < 1) startBin = 1;  // Skip DC bin
        if (endBin <= startBin) endBin = startBin + 1;
        
        float sumL = 0, sumR = 0;
        int count = 0;
        
        for (int bin = startBin; bin < endBin && bin < halfSamples; bin++) {
            sumL += vRealL[bin];
            sumR += vRealR[bin];
            count++;
        }
        
        if (count > 0) {
            sumL /= count;
            sumR /= count;
        }
        
        // Normalize: raw FFT magnitudes can be 0..~50000+
        // Use log scale for perceptual loudness mapping
        // log10(1) = 0, log10(100) = 2, log10(10000) = 4, log10(50000) ≈ 4.7
        float normL = 0, normR = 0;
        if (sumL > 1.0f) normL = log10f(sumL) / 3.0f;
        if (sumR > 1.0f) normR = log10f(sumR) / 3.0f;
        
        // Clamp 0..1
        if (normL > 1.0f) normL = 1.0f;
        if (normR > 1.0f) normR = 1.0f;
        
        bandValuesL[i] = normL;
        bandValuesR[i] = normR;
    }
}
