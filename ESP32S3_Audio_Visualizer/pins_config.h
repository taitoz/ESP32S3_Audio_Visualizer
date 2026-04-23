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
// Touch controller lives on the SHARED I2C bus (see I2C_SDA/I2C_SCL below).
// GPIO 15/10 are exposed externally on the Qwiic connector too.
#define TOUCH_IICSDA           15   // == I2C_SDA (shared)
#define TOUCH_IICSCL           10   // == I2C_SCL (shared)
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

// ─── Ambient Light Sensor (Analog) ─────────────────────────────────────────
// Analog light sensor (e.g. LDR voltage divider or phototransistor) for
// automatic display brightness adjustment.
//   Sensor output → GPIO9 (ADC1_CH8)
//   Bright = high ADC value, Dark = low ADC value
//   (Invert in software if your sensor wiring is opposite)
#define LIGHT_SENSOR_PIN       9   // GPIO9 = ADC1_CH8
#define LIGHT_SENSOR_CHANNEL   ADC_CHANNEL_8

// ─── Shared I2C Bus (internal touch + external Qwiic) ─────────────────────
// VERIFIED by on-board I2C scanner: the Qwiic connector and the internal
// capacitive-touch controller share the SAME I2C bus on GPIO 15/10.
// Bootup scan confirmed responders:
//   0x3B  — AXS15231B touch controller (on-board)
//   0x68  — DS3231 RTC                 (external, via Qwiic)
//   0x6A  — DS3231 temperature alias / AT24C32 EEPROM (on same breakout)
//
// Therefore ONE TwoWire instance (`Wire`) is used for everything: touch,
// RTC, AK4493 DAC, and anything else plugged into Qwiic.
#define I2C_SDA                15   // shared SDA
#define I2C_SCL                10   // shared SCL
#define I2C_FREQ               400000  // 400 kHz

// ─── AK4493 DAC (I2C Control, I2S Audio from external master) ──────────────
// Control interface: I2C on shared Qwiic bus (see I2C_SDA/I2C_SCL above).
// I2C address is set by CAD0/CAD1 strap pins on the AK4493 (typical: 0x10 or 0x11).
// Audio data: I2S lines are driven by EXTERNAL master (Amanero USB-I2S board),
// ESP32 does NOT generate I2S — it only reads analog audio via ADC pins 3 and 4.
#define AK4493_I2C_ADDR        0x10  // Adjust if your CAD strap pins differ
#define DAC_RESET_PIN          5     // DAC hardware reset (active LOW, new pin)
#define AMP_MUTE_RELAY_PIN     37    // MOSFET gate for amplifier power/mute relay

// ─── RTC DS3231 (I2C) ──────────────────────────────────────────────────────
// Shares the main Qwiic I2C bus with the DAC (SDA=18, SCL=17).
// DS3231 standard I2C address: 0x68.
#define RTC_I2C_FREQ           I2C_FREQ

// ─── Display Dimensions ────────────────────────────────────────────────────
#define SCREEN_WIDTH           640
#define SCREEN_HEIGHT          180
