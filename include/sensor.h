#pragma once

#include <Arduino.h>

bool readSingleSensorValue(uint16_t& dist_mm);
bool readWaterSensor(uint8_t sampleCount = 10);
void resetSensorUART();
void simulateWaterSensor();
void handleSerialWaterLevel();
bool isTankFullByBlind();
