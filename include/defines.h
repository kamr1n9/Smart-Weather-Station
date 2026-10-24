#pragma once

/***********************
 * PIN MAP
 ***********************/
#define I2C_SDA_PIN            9
#define I2C_SCL_PIN            10

#define ADC_BATTERY_PIN        17    // ADC input (voltage divider output)
#define BUTTON_PIN             3     // Active-low button; pulled-up; ISR on falling edge

/***********************
 * VOLTAGE DIVIDER
 *  Vbat ---- R1 ----+---- R2 ---- GND
 *                   |
 *              ADC (GPIO17)
 *
 *  Vbat = Vadc * (R1 + R2) / R2
 ***********************/
#define R1_OHMS                27000.0f   // 27 kΩ (top to battery)
#define R2_OHMS                100000.0f  // 100 kΩ (bottom to GND)
#define ADC_ATTENUATION_DB     ADC_11db   // up to ~3.6 V on ESP32-S3
#define ADC_MAX_MV             3300.0f    // analogReadMilliVolts() returns mV at pin

/***********************
 * BATTERY GAUGE MODEL (simple linear, tweak if needed)
 ***********************/
#define BAT_VOLTAGE_EMPTY      3.30f      // V where 0% shown
#define BAT_VOLTAGE_FULL       4.20f      // V where 100% shown
#define BAT_PCT_FLOOR          0          // clamp floor
#define BAT_PCT_CEIL           100        // clamp ceil

/***********************
 * SENSOR + LOOP CADENCE
 ***********************/
#define SENSOR_POLL_SECONDS    2          // <== Change this to adjust sensor/website update rate
#define WIFI_PORT              80

/***********************
 * WIFI (SoftAP) CONFIG
 ***********************/
#define WIFI_AP_SSID           "SmartWeatherNode" // SSID for ESP32 AP mode
#define WIFI_AP_PASS           "TheWeatherGuy"   // 8+ chars recommended; set to "" for open not recommended

/***********************
 * TASKING / CORES
 ***********************/
#define CORE_SENSOR            0
#define CORE_NETWORK           1
#define STACK_SENSOR_TASK      4096
#define STACK_NETWORK_TASK     6144

/***********************
 * MISC
 ***********************/
#define DEVICE_NAME            "Smart Weather Node"
#define HTTP_JSON_PATH         "/api"
#define HTTP_ROOT_PATH         "/"

/***********************
 * DEBUG PRINT CONTROL
 ***********************/
#define DEBUG_SERIAL_PRINT    1    // Set to 1 to enable debug prints, 0 to disable
#define DEBUG_PRINT(x)        do { if (DEBUG_SERIAL_PRINT) debugPrint(String(x)); } while(0)