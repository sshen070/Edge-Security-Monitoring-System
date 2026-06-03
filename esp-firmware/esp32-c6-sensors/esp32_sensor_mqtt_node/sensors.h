#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

const int LIGHT_PIN = 3;

struct SensorData {

//  float temperature;
  int light;
//  bool motion;
  int rssi;
  uint32_t uptime;
  uint32_t connection_time_ms;
};

void initSensors();

SensorData readSensors();

String buildPayload(const SensorData &data);

void markConnectionStart();

#endif
