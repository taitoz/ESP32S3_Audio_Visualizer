#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "audio_sampling.h"

/*******************************************************************************
 * FFT Spectrum Analyzer — processes vReal/vImag and produces frequency bands
 ******************************************************************************/

#define NUM_BANDS          32        // number of frequency bands displayed
#define BAND_SMOOTHING     0.7f      // exponential smoothing factor (0=instant, 1=frozen)
#define PEAK_FALL_RATE     0.5f      // peak dot fall speed per frame
#define PEAK_HOLD_FRAMES   15        // frames to hold peak before falling

extern float bandValues[NUM_BANDS];       // current magnitude per band (smoothed)
extern float peakValues[NUM_BANDS];       // peak hold per band
extern int   peakHoldCount[NUM_BANDS];    // frames remaining for peak hold

void spectrum_init();
void spectrum_compute_fft();              // run FFT on vReal/vImag, populate bandValues
void spectrum_draw_bars(TFT_eSprite &spr); // draw bar-style spectrum analyzer
