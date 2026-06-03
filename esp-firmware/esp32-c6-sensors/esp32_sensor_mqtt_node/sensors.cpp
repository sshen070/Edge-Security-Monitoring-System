#include "sensors.h"
//#include "config.h"

#include <WiFi.h>

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

  String payload = "{";

  payload += "\"light\":";
  payload += String(data.light);
  payload += ",";

//  payload += "\"temperature\":";
//  payload += String(data.temperature, 1);
//  payload += ",";

  payload += "\"rssi\":";
  payload += String(data.rssi);
  payload += ",";

  payload += "\"uptime_ms\":";
  payload += String(data.uptime);
  payload += ",";

  payload += "\"connection_time_ms\":";
  payload += String(data.connection_time_ms);

  payload += "}";

  return payload;
}
