#ifndef TRACKER_CONFIG_H
#define TRACKER_CONFIG_H
/* Verified main-board pins; motor signs require an assembly direction test. */
#define CORE_HZ 64000000u
#define PAN_IN1_PIN 3u
#define PAN_IN2_PIN 8u
#define PAN_IN3_PIN 1u
#define PAN_IN4_PIN 0u
#define TILT_IN1_PIN 7u
#define TILT_IN2_PIN 6u
#define TILT_IN3_PIN 5u
#define TILT_IN4_PIN 4u
#define PAN_PHASE_SIGN 1
#define TILT_PHASE_SIGN 1
#define SENSOR_LEFT_ADC 0u
#define SENSOR_UP_ADC 1u
#define SENSOR_RIGHT_ADC 2u
#define SENSOR_DOWN_ADC 3u
#define ADC_REFERENCE_MV 3300u /* nominal VDDA; calibrate on the board */
#define KEY_LEFT_PIN 5u
#define KEY_DOWN_PIN 6u
#define KEY_RIGHT_PIN 7u
#define KEY_UP_PIN 8u
#define KEY_MODE_PIN 11u
#define LCD_SCK_PIN 10u
#define LCD_MOSI_PIN 11u
#define LCD_RESET_PIN 12u
#define LCD_DC_PIN 13u
#define LCD_CS_PIN 14u
#define LCD_BACKLIGHT_PIN 15u
#define LCD_X_OFFSET 2u /* panel-variant parameter; verify real display */
#define LCD_Y_OFFSET 3u
#define LCD_INVERT 0u
#define LCD_RGB_ORDER 0u
/* Conservative software travel caps, NOT measured stops. Zero=power-on pose. */
#define PAN_SOFT_LIMIT_STEPS 768
#define TILT_SOFT_LIMIT_STEPS 384
#define STEP_FAST_MS 4u
#define STEP_START_MS 9u
#define MANUAL_TAP_STEPS 16
#define MANUAL_HOLD_MS 350u
#define KEY_DEBOUNCE_MS 25u
#define MODE_LONG_PRESS_MS 1500u
#define TRACK_START_ERROR 40
#define TRACK_STOP_ERROR 20
#define TRACK_MIN_LIGHT 20u
/* Linear voltage scale only, not measured remaining battery capacity. */
#define BATTERY_EMPTY_MV 3000u
#define BATTERY_FULL_MV 4200u
#define LOW_BATTERY_PERCENT 45u
#endif
