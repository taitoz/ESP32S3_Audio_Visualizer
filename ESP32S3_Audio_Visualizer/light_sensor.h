#pragma once

#include <Arduino.h>
#include "pins_config.h"
#include "settings.h"

/*******************************************************************************
 * Ambient Light Sensor — automatic display brightness adjustment
 * 
 * Hardware: analog light sensor (LDR divider / phototransistor) on GPIO5
 *   Bright ambient light → high ADC value → high backlight PWM
 *   Dark ambient → low ADC value → low backlight PWM
 * 
 * Reads are smoothed with exponential moving average to avoid flicker.
 * Polling rate: ~5 Hz (called from Core 0 task).
 ******************************************************************************/

#define LIGHT_SENSOR_SMOOTH     0.15f    // EMA alpha (0=frozen, 1=instant)
#define LIGHT_SENSOR_POLL_MS    200      // poll interval (5 Hz)

void light_sensor_init();
void light_sensor_poll();    // call periodically from Core 0 task
int  light_sensor_raw();     // last smoothed ADC reading (0–4095)
uint8_t light_sensor_brightness();  // computed brightness PWM value
