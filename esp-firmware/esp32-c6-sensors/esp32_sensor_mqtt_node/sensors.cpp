#include "sensors.h"
//#include "config.h"

#include <WiFi.h>
#include <ArduinoJson.h>


static uint32_t connectionStartTime = 0;

void initSensors() {
  pinMode(LIGHT_PIN, INPUT);
}

// Call this AFTER WiFi connects
void markConnectionStart() {
  connectionStartTime = millis();
}

SensorData readSensors() {

  SensorData data;

  data.device_id = "esp32-c6";

  // Light & motion read
  data.light = analogRead(LIGHT_PIN);
  
  // Placeholder temperature
//  data.temperature = random(200, 350) / 10.0;

  data.rssi = WiFi.RSSI();
  data.uptime = millis();

  // Connection duration tracking
  if (connectionStartTime == 0) {
    data.connection_time_ms = 0;
  } 
  else {
    data.connection_time_ms = millis() - connectionStartTime;
  }

  return data;
}

String buildPayload(const SensorData &data) {
  StaticJsonDocument<256> doc;

  doc["device_id"] = data.device_id;
  doc["light"] = data.light;
  doc["rssi"] = data.rssi;
  doc["uptime_ms"] = data.uptime;
  doc["connection_time_ms"] = data.connection_time_ms;

  String output;
  serializeJson(doc, output);
  return output;
}
