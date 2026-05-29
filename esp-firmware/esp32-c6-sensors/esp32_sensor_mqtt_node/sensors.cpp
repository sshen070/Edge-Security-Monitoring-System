#include "sensors.h"
#include "config.h"

#include <WiFi.h>

void initSensors() {
  pinMode(PIR_PIN, INPUT);
}

SensorData readSensors() {

  SensorData data;

  // Light & motion read
  data.light = analogRead(LIGHT_PIN);
  data.motion = digitalRead(PIR_PIN);

  // Placeholder temperature
  data.temperature = random(200, 350) / 10.0;

  data.rssi = WiFi.RSSI();
  data.uptime = millis();

  return data;
}

String buildPayload(const SensorData &data) {

  String payload = "{";

  payload += "\"light\":";
  payload += String(data.light);
  payload += ",";

  payload += "\"motion\":";
  payload += String(data.motion ? "true" : "false");
  payload += ",";

  payload += "\"rssi\":";
  payload += String(data.rssi);
  payload += ",";

  payload += "\"uptime_ms\":";
  payload += String(data.uptime);

  payload += "}";

  return payload;
}