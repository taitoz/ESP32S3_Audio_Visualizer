#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "audio_sampling.h"

/*******************************************************************************
 * FFT Spectrum Analyzer — Stereo (L + R), processes vRealL/R and produces
 * frequency bands per channel
 ******************************************************************************/

#define NUM_BANDS          32        // number of frequency bands per channel
#define BAND_SMOOTHING     0.7f      // exponential smoothing factor (0=instant, 1=frozen)
#define PEAK_FALL_RATE     0.5f      // peak dot fall speed per frame
#define PEAK_HOLD_FRAMES   15        // frames to hold peak before falling

extern float bandValuesL[NUM_BANDS];      // Left channel magnitude per band (smoothed)
extern float peakValuesL[NUM_BANDS];      // Left channel peak hold per band
extern float bandValuesR[NUM_BANDS];      // Right channel magnitude per band (smoothed)
extern float peakValuesR[NUM_BANDS];      // Right channel peak hold per band

// Precomputed lookup tables for safe optimization (reduced memory)
extern float hannWindow[SAMPLES];          // Precomputed Hann window coefficients
extern int   binToBand[256];               // FFT bin → frequency band mapping (reduced size)

void spectrum_init();
void spectrum_compute_fft();              // run FFT on both L and R channels
void spectrum_draw_bars(TFT_eSprite &spr); // draw stereo bar-style spectrum analyzer
