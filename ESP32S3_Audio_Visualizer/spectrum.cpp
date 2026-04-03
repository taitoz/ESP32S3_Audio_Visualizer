#include "spectrum.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

/*******************************************************************************
 * Minimal Spectrum Module - Simple FFT Implementation
 * 
 * Provides basic FFT computation for Technics EQ mode without external dependencies.
 * Uses simplified DFT for compatibility and stability.
 ******************************************************************************/

// ─── External Variables (from audio_sampling) ───────────────────────────────
extern float vRealL[SAMPLES];
extern float vImagL[SAMPLES];
extern float vRealR[SAMPLES];
extern float vImagR[SAMPLES];

// ─── Band Values (FFT output) ───────────────────────────────────────────────
float bandValuesL[NUM_BANDS] = {0};
float bandValuesR[NUM_BANDS] = {0};

// ─── Simple Window Function ─────────────────────────────────────────────────
static void apply_hamming_window(float *data, int samples) {
    for (int i = 0; i < samples; i++) {
        float multiplier = 0.54f - 0.46f * cosf(2.0f * PI * i / (samples - 1));
        data[i] *= multiplier;
    }
}

// ─── Simple DFT (not FFT but sufficient for visualization) ──────────────────
static void simple_dft(float *realIn, float *realOut, int samples) {
    for (int k = 0; k < samples / 2; k++) {
        float sumReal = 0;
        float sumImag = 0;
        
        for (int n = 0; n < samples; n++) {
            float angle = -2.0f * PI * k * n / samples;
            sumReal += realIn[n] * cosf(angle);
            sumImag += realIn[n] * sinf(angle);
        }
        
        // Magnitude
        realOut[k] = sqrtf(sumReal * sumReal + sumImag * sumImag);
    }
}

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
    // Apply windowing
    apply_hamming_window(vRealL, SAMPLES);
    apply_hamming_window(vRealR, SAMPLES);
    
    // Simple DFT for both channels
    static float magnitudeL[SAMPLES/2];
    static float magnitudeR[SAMPLES/2];
    
    simple_dft(vRealL, magnitudeL, SAMPLES);
    simple_dft(vRealR, magnitudeR, SAMPLES);
    
    // Convert DFT bins to frequency bands
    int halfSamples = SAMPLES / 2;
    for (int i = 0; i < NUM_BANDS; i++) {
        int startBin = i * halfSamples / NUM_BANDS;
        int endBin = (i + 1) * halfSamples / NUM_BANDS;
        
        float sumL = 0, sumR = 0;
        int count = 0;
        
        for (int bin = startBin; bin < endBin && bin < halfSamples; bin++) {
            sumL += magnitudeL[bin];
            sumR += magnitudeR[bin];
            count++;
        }
        
        if (count > 0) {
            bandValuesL[i] = sumL / count;
            bandValuesR[i] = sumR / count;
        }
    }
}
