#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>


const int PIR_PIN = D0;
const int LIGHT_PIN = A0;

struct SensorData {

  float temperature;
  int light;
  bool motion;
  int rssi;
  uint32_t uptime;
};

void initSensors();

SensorData readSensors();

String buildPayload(const SensorData &data);

#endif