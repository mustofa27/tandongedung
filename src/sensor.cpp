#include "sensor.h"
#include "globals.h"
#include "pump.h"
#include <math.h>

/* ===== Internal clustering helpers (file-private) ===== */

static int compareUint16(const void* a, const void* b) {
  return (*(uint16_t*)a - *(uint16_t*)b);
}

static void removeOutliersUsingClustering(uint16_t* readings, uint8_t count, uint16_t& result) {
  if (count == 0) return;
  qsort(readings, count, sizeof(uint16_t), compareUint16);

  uint16_t median = (count % 2 == 0)
    ? (readings[count / 2 - 1] + readings[count / 2]) / 2
    : readings[count / 2];

  uint16_t deviations[30];
  for (uint8_t i = 0; i < count; i++)
    deviations[i] = (readings[i] > median) ? (readings[i] - median) : (median - readings[i]);

  qsort(deviations, count, sizeof(uint16_t), compareUint16);
  uint16_t mad = (count % 2 == 0)
    ? (deviations[count / 2 - 1] + deviations[count / 2]) / 2
    : deviations[count / 2];
  if (mad < 3) mad = 3;

  uint16_t threshold   = mad;
  uint32_t sumSquares  = 0;
  uint8_t  clusterCount = 0;
  for (uint8_t i = 0; i < count; i++) {
    uint16_t dev = (readings[i] > median) ? (readings[i] - median) : (median - readings[i]);
    if (dev <= threshold) {
      uint32_t v = readings[i];
      sumSquares += v * v;
      clusterCount++;
    }
  }
  result = (clusterCount > 0)
    ? (uint16_t)sqrtf((float)sumSquares / clusterCount)
    : median;
}

static void removeOutliersUsing3Means(uint16_t* readings, uint8_t count, uint16_t& result) {
  if (count < 3) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < count; i++) sum += readings[i];
    result = sum / count;
    return;
  }
  qsort(readings, count, sizeof(uint16_t), compareUint16);

  // Fall back to MAD clustering if the data spread is too narrow for 3 clusters
  if ((readings[count - 1] - readings[0]) < 20) {
    removeOutliersUsingClustering(readings, count, result);
    return;
  }

  // Initialise 3 centroids at the 1/6, 3/6, 5/6 quantiles
  uint16_t c1 = readings[count / 6];
  uint16_t c2 = readings[count / 2];
  uint16_t c3 = readings[(5 * count) / 6];

  for (uint8_t iter = 0; iter < 5; iter++) {
    uint32_t sum1 = 0, sum2 = 0, sum3 = 0;
    uint8_t  cnt1 = 0, cnt2 = 0, cnt3 = 0;
    for (uint8_t i = 0; i < count; i++) {
      uint16_t d1 = (readings[i] > c1) ? (readings[i] - c1) : (c1 - readings[i]);
      uint16_t d2 = (readings[i] > c2) ? (readings[i] - c2) : (c2 - readings[i]);
      uint16_t d3 = (readings[i] > c3) ? (readings[i] - c3) : (c3 - readings[i]);
      if      (d1 <= d2 && d1 <= d3) { sum1 += readings[i]; cnt1++; }
      else if (d2 <= d1 && d2 <= d3) { sum2 += readings[i]; cnt2++; }
      else                           { sum3 += readings[i]; cnt3++; }
    }
    if (cnt1 > 0) c1 = sum1 / cnt1;
    if (cnt2 > 0) c2 = sum2 / cnt2;
    if (cnt3 > 0) c3 = sum3 / cnt3;
  }

  // Pick the largest cluster and return its RMS
  uint32_t sq1 = 0, sq2 = 0, sq3 = 0;
  uint8_t  cnt1 = 0, cnt2 = 0, cnt3 = 0;
  for (uint8_t i = 0; i < count; i++) {
    uint16_t d1 = (readings[i] > c1) ? (readings[i] - c1) : (c1 - readings[i]);
    uint16_t d2 = (readings[i] > c2) ? (readings[i] - c2) : (c2 - readings[i]);
    uint16_t d3 = (readings[i] > c3) ? (readings[i] - c3) : (c3 - readings[i]);
    uint32_t v  = readings[i];
    if      (d1 <= d2 && d1 <= d3) { sq1 += v * v; cnt1++; }
    else if (d2 <= d1 && d2 <= d3) { sq2 += v * v; cnt2++; }
    else                           { sq3 += v * v; cnt3++; }
  }
  if      (cnt1 >= cnt2 && cnt1 >= cnt3 && cnt1 > 0) result = (uint16_t)sqrtf((float)sq1 / cnt1);
  else if (cnt2 >= cnt1 && cnt2 >= cnt3 && cnt2 > 0) result = (uint16_t)sqrtf((float)sq2 / cnt2);
  else if (cnt3 > 0)                                 result = (uint16_t)sqrtf((float)sq3 / cnt3);
  else                                               result = readings[count / 2];
}

/* ===== Public functions ===== */

bool readSingleSensorValue(uint16_t& dist_mm) {
  static uint8_t buffer[4];
  static uint8_t index = 0;
  unsigned long  startWait = millis();

  while (millis() - startWait < 500) {
    while (sensorSerial.available()) {
      uint8_t b = sensorSerial.read();
      if (index == 0 && b != 0xFF) continue;
      buffer[index++] = b;

      if (index == 4) {
        index = 0;
        uint8_t checksum = buffer[0] + buffer[1] + buffer[2];
        if (checksum != buffer[3]) return false;

        uint16_t raw = (buffer[1] << 8) | buffer[2];
        if (raw == 0xFFFF) return false;  // blind zone / no echo
        if (raw == 0x0000) return false;  // invalid
        if (raw < 280 || raw > 7500) return false;  // out of A01NYUB spec

        dist_mm = raw;
        return true;
      }
    }
    delay(10);
  }
  return false;
}

bool readWaterSensor(uint8_t sampleCount) {
  if (sampleCount < 1)  sampleCount = 1;
  if (sampleCount > 30) sampleCount = 30;

  // Hold-last-valid state (private to this function)
  static float         lastValidWaterLevel_mm = -1.0f;
  static unsigned long lastValidTime          = 0;

  uint16_t readings[30];
  uint8_t  validReadings = 0;

  for (uint8_t i = 0; i < sampleCount; i++) {
    uint16_t dist;
    if (readSingleSensorValue(dist)) {
      readings[validReadings++] = dist;
      Serial.printf("[SENSOR] Sample %d: %d mm\n", i + 1, dist);
    } else {
      i--;
    }
    if (i < sampleCount - 1) delay(50);
  }

  // All samples rejected — check if last valid reading suggests blind zone
  if (validReadings == 0) {
    if (lastValidTime > 0 &&
        millis() - lastValidTime < 30000 &&
        lastValidWaterLevel_mm >= (HEIGHT_TANK_MM - BLIND_DISTANCE_MM * 1.5f)) {
      blindActive      = true;
      waterLevel_mm    = HEIGHT_TANK_MM;
      waterLevel_cm    = waterLevel_mm / 10.0f;
      waterLevel_m     = waterLevel_mm / 1000.0f;
      lastSensorMillis = millis();
      sensorOnline     = true;
      Serial.println("[SENSOR] All samples invalid → assume BLIND ZONE (tank full)");
      return true;
    }
    return false;
  }

  // Select outlier removal based on sample count
  if (validReadings < 3) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < validReadings; i++) sum += readings[i];
    distance_mm = sum / validReadings;
  } else if (validReadings >= 7) {
    removeOutliersUsing3Means(readings, validReadings, distance_mm);
  } else {
    removeOutliersUsingClustering(readings, validReadings, distance_mm);
  }

  // Blind zone: sensor too close to water surface
  if ((float)distance_mm < BLIND_DISTANCE_MM) {
    blindActive      = true;
    waterLevel_mm    = HEIGHT_TANK_MM;
    waterLevel_cm    = waterLevel_mm / 10.0f;
    waterLevel_m     = waterLevel_mm / 1000.0f;
    lastSensorMillis = millis();
    sensorOnline     = true;
    return true;
  }

  float calculated = HEIGHT_TANK_MM - (float)distance_mm;

  // Out-of-range: fall back to hold-last-valid
  if (calculated < 0 || calculated > HEIGHT_TANK_MM * 1.05f) {
    if (lastValidTime > 0 && millis() - lastValidTime < 10000) {
      waterLevel_mm    = lastValidWaterLevel_mm;
      waterLevel_cm    = waterLevel_mm / 10.0f;
      waterLevel_m     = waterLevel_mm / 1000.0f;
      lastSensorMillis = millis();
      sensorOnline     = true;
      Serial.println("[SENSOR] Reading rejected → hold-last-valid");
      return true;
    }
    Serial.printf("Distance reading %d mm out of range → \n", distance_mm);
    Serial.println("[SENSOR] Reading rejected, no last-valid → skip");
    return false;
  }

  // Valid reading
  blindActive            = false;
  waterLevel_mm          = calculated;
  waterLevel_cm          = waterLevel_mm / 10.0f;
  waterLevel_m           = waterLevel_mm / 1000.0f;
  lastValidWaterLevel_mm = waterLevel_mm;
  lastValidTime          = millis();
  lastSensorMillis       = millis();
  if (!sensorOnline) Serial.println("[SENSOR ONLINE]");
  sensorOnline = true;
  return true;
}

void resetSensorUART() {
  Serial.println("[UART] Resetting sensor UART...");
  sensorSerial.end();
  delay(200);
  sensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  sensorOnline = true;
}

void simulateWaterSensor() {
  if (manualOverride) return;
  static float          sim_mm  = 2000.0f;
  static bool           turun   = true;
  static unsigned long  lastSim = 0;

  if (millis() - lastSim < 5000) return;
  lastSim = millis();

  if (turun) { sim_mm -= 150.0f; if (sim_mm <= 0)             turun = false; }
  else       { sim_mm += 150.0f; if (sim_mm > HEIGHT_TANK_MM) turun = true;  }

  sim_mm           = constrain(sim_mm, 0.0f, HEIGHT_TANK_MM);
  waterLevel_mm    = sim_mm;
  waterLevel_cm    = sim_mm / 10.0f;
  waterLevel_m     = sim_mm / 1000.0f;
  distance_mm      = (uint16_t)(HEIGHT_TANK_MM - sim_mm);
  blindActive      = (distance_mm <= BLIND_DISTANCE_MM);
  sensorOnline     = true;
  lastSensorMillis = millis();
}

void handleSerialWaterLevel() {
  if (!Serial.available()) return;
  manualOverride = true;
  String input = Serial.readStringUntil('\n');
  input.trim();
  float val = input.toFloat();
  if (val <= 0 || val > HEIGHT_TANK_M) {
    Serial.println("[CFG] Water level invalid!");
    return;
  }
  waterLevel_m  = val;
  waterLevel_cm = waterLevel_m * 100.0f;
  waterLevel_mm = waterLevel_m * 1000.0f;
  Serial.printf("[CFG] Water level updated: %.2f m | %.2f cm | %.0f mm\n",
                waterLevel_m, waterLevel_cm, waterLevel_mm);
  updatePumpLogic();
}

bool isTankFullByBlind() {
  return sensorOnline && blindActive;
}
