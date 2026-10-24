#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_BME280.h>   // Use this if you have BME280 sensor
// #include <Adafruit_BMP280.h>   // Use this if you have a BMP280 sensor
#include "defines.h"

// Shared payload for UI/API
struct Telemetry {
  float tmp_c = NAN;
  float pres_hpa  = NAN;
  float hum_pct  = NAN;
  float bat_v    = NAN;
  uint8_t bat_pct    = 0;
  unsigned long last_ms = 0;
};

extern Telemetry g_data;
extern portMUX_TYPE g_data_mux;   // lightweight lock for cross-core access

// ISR prototype
void IRAM_ATTR onButtonFalling();

// Debug print function
void debugPrint(const String& message);

// Helpers
float readBatteryVolts();
uint8_t voltsToPercent(float vbat);
void formatJson(String& out);

// Tasks
void sensorTask(void* arg);
void networkTask(void* arg);
