#pragma once

/*******************************************************************************
 * Pin Configuration for LilyGo T-Display-S3-Long (Touchscreen version)
 * Project: ESP32-S3 Audio Visualizer + AK4493 DAC + BLE GearVR HID
 ******************************************************************************/

// ─── QSPI Display ───────────────────────────────────────────────────────────
#define SPI_FREQUENCY         32000000
#define TFT_SPI_MODE          SPI_MODE0
#define TFT_SPI_HOST          SPI2_HOST

#define SEND_BUF_SIZE         (28800/2)
#define TFT_QSPI_CS           12
#define TFT_QSPI_SCK          17
#define TFT_QSPI_D0           13
#define TFT_QSPI_D1           18
#define TFT_QSPI_D2           21
#define TFT_QSPI_D3           14
#define TFT_QSPI_RST          16
#define TFT_BL                 1

// ─── Buttons & Battery ──────────────────────────────────────────────────────
#define PIN_BAT_VOLT           8
#define PIN_BUTTON_1           0
#define PIN_BUTTON_2           21

// ─── Touch Screen (I2C) ────────────────────────────────────────────────────
#define TOUCH_IICSDA           15
#define TOUCH_IICSCL           10
#define TOUCH_INT              11
#define TOUCH_RES              16

// Touch data parsing
#define AXS_TOUCH_ONE_POINT_LEN             6
#define AXS_TOUCH_BUF_HEAD_LEN              2
#define AXS_TOUCH_GESTURE_POS               0
#define AXS_TOUCH_POINT_NUM                 1
#define AXS_TOUCH_EVENT_POS                 2
#define AXS_TOUCH_X_H_POS                   2
#define AXS_TOUCH_X_L_POS                   3
#define AXS_TOUCH_ID_POS                    4
#define AXS_TOUCH_Y_H_POS                   4
#define AXS_TOUCH_Y_L_POS                   5
#define AXS_TOUCH_WEIGHT_POS                6
#define AXS_TOUCH_AREA_POS                  7

#define AXS_GET_POINT_NUM(buf)    buf[AXS_TOUCH_POINT_NUM]
#define AXS_GET_GESTURE_TYPE(buf) buf[AXS_TOUCH_GESTURE_POS]
#define AXS_GET_POINT_X(buf,point_index) (((uint16_t)(buf[AXS_TOUCH_ONE_POINT_LEN*point_index+AXS_TOUCH_X_H_POS] & 0x0F) <<8) + (uint16_t)buf[AXS_TOUCH_ONE_POINT_LEN*point_index+AXS_TOUCH_X_L_POS])
#define AXS_GET_POINT_Y(buf,point_index) (((uint16_t)(buf[AXS_TOUCH_ONE_POINT_LEN*point_index+AXS_TOUCH_Y_H_POS] & 0x0F) <<8) + (uint16_t)buf[AXS_TOUCH_ONE_POINT_LEN*point_index+AXS_TOUCH_Y_L_POS])
#define AXS_GET_POINT_EVENT(buf,point_index) (buf[AXS_TOUCH_ONE_POINT_LEN*point_index+AXS_TOUCH_EVENT_POS] >> 6)

// ─── Audio ADC Input (Stereo) ───────────────────────────────────────────────
// Two channels via audio transformers, each with its own bias network:
//   Transformer secondary → 100nF cap → GPIO pin
//   Bias: 2x 100k resistors from 3.3V and GND to pin (sets DC midpoint ~1.65V)
//
//   LEFT channel:  Audio Transformer L → 100nF → GPIO3
//                                                  ├─ 100k → 3.3V
//                                                  └─ 100k → GND
//
//   RIGHT channel: Audio Transformer R → 100nF → GPIO4
//                                                  ├─ 100k → 3.3V
//                                                  └─ 100k → GND
//
#define AUDIO_ADC_PIN_L        3   // GPIO3 = ADC1_CH2 — Left channel
#define AUDIO_ADC_CHANNEL_L    ADC_CHANNEL_2
#define AUDIO_ADC_PIN_R        4   // GPIO4 = ADC1_CH3 — Right channel
#define AUDIO_ADC_CHANNEL_R    ADC_CHANNEL_3
#define AUDIO_NUM_CHANNELS     2

// ─── AK4493 DAC (SPI Control) ──────────────────────────────────────────────
// Uses HSPI (SPI3) — separate from display QSPI (SPI2)
#define AK4493_SPI_HOST        SPI3_HOST
#define AK4493_SPI_SCK         39
#define AK4493_SPI_MOSI        40
#define AK4493_SPI_MISO        41  // SDO from AK4493 for register readback
#define AK4493_SPI_CS          42
#define AK4493_PDN             -1  // connect to power-down pin if used, -1 = not connected
#define AK4493_SPI_FREQ        1000000  // 1 MHz SPI clock for AK4493

// ─── Display Dimensions ────────────────────────────────────────────────────
#define SCREEN_WIDTH           640
#define SCREEN_HEIGHT          180
