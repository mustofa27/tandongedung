#pragma once

#include <Arduino.h>

bool readWaterSensor();  // non-blocking; true when a new level was computed
void resetSensorUART();
bool simulateWaterSensor();
void handleSerialWaterLevel();
bool isTankFullByBlind();
