#include "sensor.h"
#include "globals.h"
#include "pump.h"
#include "device_config.h"

/* ===== Internal state (file-private) ===== */

// Samples collected during the current update window
static uint16_t      samples[SENSOR_MAX_SAMPLES];
static uint8_t       sampleCount    = 0;
static uint16_t      framesReceived = 0;  // valid frames this window, incl. range-rejected
static unsigned long windowStart    = 0;

// 1D Kalman filter on distance (mm)
static bool    kfInit    = false;
static float   kfX       = 0.0f;  // estimate
static float   kfP       = 0.0f;  // estimate variance
static uint8_t kfRejects = 0;     // consecutive gated measurements
static float   noiseVar  = 0.0f;  // running variance of accepted innovations (mm^2)

/* ===== Internal helpers ===== */

static int compareUint16(const void* a, const void* b) {
  return (int)*(uint16_t*)a - (int)*(uint16_t*)b;
}

static uint16_t medianOf(uint16_t* v, uint8_t n) {
  qsort(v, n, sizeof(uint16_t), compareUint16);
  return (n % 2 == 0) ? (v[n / 2 - 1] + v[n / 2]) / 2 : v[n / 2];
}

// MAD-gated mean: drop samples further than MAD_GATE robust sigmas from the
// median, average the rest. Expects v sorted (medianOf sorts it).
static float madMean(const uint16_t* v, uint8_t n, uint16_t median, uint8_t& kept) {
  uint16_t dev[SENSOR_MAX_SAMPLES];
  for (uint8_t i = 0; i < n; i++)
    dev[i] = (v[i] > median) ? (v[i] - median) : (median - v[i]);
  float sigma = max(medianOf(dev, n) * 1.4826f, MAD_MIN_SIGMA_MM);
  float limit = MAD_GATE * sigma;

  uint32_t sum = 0;
  kept = 0;
  for (uint8_t i = 0; i < n; i++) {
    float d = (float)v[i] - median;
    if (fabsf(d) <= limit) { sum += v[i]; kept++; }
  }
  return kept > 0 ? (float)sum / kept : median;
}

// Default FF-protocol frame (ff_uart_auto): [0xFF, H, L, SUM].
// Non-blocking: only reads when a full frame is buffered.
static bool readFrame(uint16_t& dist_mm) {
  while (sensorSerial.available() >= 4) {
    if (sensorSerial.peek() != 0xFF) { sensorSerial.read(); continue; }  // resync to header

    uint8_t recv_buf[4] = { 0 };
    sensorSerial.readBytes(recv_buf, 4);
    uint8_t sum = recv_buf[0] + recv_buf[1] + recv_buf[2];
    if (sum != recv_buf[3]) continue;

    dist_mm = recv_buf[1] << 8 | recv_buf[2];
    return true;
  }
  return false;
}

// Returns false if the measurement was rejected by the outlier gate
static bool kalmanUpdate(float z) {
  if (!kfInit) {
    kfX = z; kfP = KF_R; kfRejects = 0; kfInit = true;
    return true;
  }

  kfP += KF_Q;  // predict: level may have drifted
  float innov = z - kfX;
  float S     = kfP + KF_R;

  if (innov * innov > KF_GATE_SIGMA * KF_GATE_SIGMA * S) {
    if (++kfRejects < KF_MAX_REJECTS) return false;
    // Persistent disagreement → real jump, re-initialise on it
    kfX = z; kfP = KF_R; kfRejects = 0;
    return true;
  }

  kfRejects = 0;
  noiseVar  = (noiseVar == 0.0f) ? innov * innov : 0.95f * noiseVar + 0.05f * innov * innov;
  float K = kfP / S;
  kfX += K * innov;
  kfP *= (1.0f - K);
  return true;
}

static void setWaterLevel(float mm) {
  waterLevel_mm = mm;
  waterLevel_cm = mm / 10.0f;
  waterLevel_m  = mm / 1000.0f;
  if (!sensorOnline) Serial.println("[SENSOR ONLINE]");
  sensorOnline = true;
}

/* ===== Public functions ===== */

bool readWaterSensor() {
  // Last filtered level, used to infer blind zone when every frame is rejected
  static float         lastValidWaterLevel_mm = -1.0f;
  static unsigned long lastValidTime          = 0;

  // 1. Drain every buffered frame into the window
  static uint16_t lastRejected   = 0;
  static uint16_t rejectedFrames = 0;
  uint16_t dist;
  while (readFrame(dist)) {
    framesReceived++;
    if (dist == 0 || dist == 0xFFFF ||                          // invalid / no echo
        dist > HEIGHT_TANK_MM + RANGE_MARGIN_MM) {              // beyond tank bottom
      lastRejected = dist;
      rejectedFrames++;
      continue;
    }
    Serial.printf(" | sensor raw=%u mm |", dist);
    if (sampleCount < SENSOR_MAX_SAMPLES) samples[sampleCount++] = dist;
  }

  // 2. Produce one level per update window
  if (millis() - windowStart < SENSOR_UPDATE_INTERVAL) return false;
  windowStart = millis();
  uint8_t  n        = sampleCount;
  uint16_t frames   = framesReceived;
  uint16_t rejected = rejectedFrames;
  sampleCount    = 0;
  framesReceived = 0;
  rejectedFrames = 0;

  if (n < SENSOR_MIN_SAMPLES) {
    // Slow sensor, nothing bad: keep the samples and finish the window next time
    if (rejected == 0 && n > 0) {
      sampleCount      = n;
      lastSensorMillis = millis();
      return false;
    }

    // Every frame rejected right after a reading at the blind-zone edge → water
    // rose into the blind zone (tank full). Once in the blind zone, stay there
    // until a valid reading returns, so a long-full tank doesn't go offline.
    bool nearBlindEdge = lastValidTime > 0 && millis() - lastValidTime < 30000 &&
                         lastValidWaterLevel_mm >= HEIGHT_TANK_MM - (SENSOR_FULL_DIST_MM + 100.0f);
    if (n == 0 && rejected > 0 && (blindActive || nearBlindEdge)) {
      blindActive      = true;
      kfInit           = false;
      lastSensorMillis = millis();
      setWaterLevel(HEIGHT_TANK_MM);
      Serial.printf("[SENSOR] All %u frames invalid (last=%u) near blind edge → assume BLIND ZONE\n",
                    rejected, lastRejected);
      return true;
    }

    if (frames == 0 && n == 0)
      Serial.printf("[SENSOR] No frames on RX%d (bytes waiting=%d) → check wiring/power\n",
                    SENSOR_RX_PIN, sensorSerial.available());
    else
      Serial.printf("[SENSOR] %u frames, %u valid, %u rejected, last rejected=%u mm (accept 1-%.0f mm)\n",
                    frames, n, rejected, lastRejected, HEIGHT_TANK_MM + RANGE_MARGIN_MM);
    return false;
  }

  lastSensorMillis = millis();  // sensor alive, even if the filter rejects this window
  uint16_t median = medianOf(samples, n);

  // 3. Blind zone on the raw median: tank-full must not wait for the filter
  if (median < SENSOR_FULL_DIST_MM) {
    blindActive = true;
    distance_mm = median;
    kfInit      = false;
    setWaterLevel(HEIGHT_TANK_MM);
    Serial.printf("[SENSOR] median=%u mm < %.0f mm → BLIND ZONE (object/water too close)\n",
                  median, SENSOR_FULL_DIST_MM);
    return true;
  }

  // 4. MAD-gated mean of the window, then Kalman filter with outlier gate
  uint8_t kept;
  float   mean = madMean(samples, n, median, kept);
  if (!kalmanUpdate(mean)) {
    Serial.printf("[SENSOR] mean=%.1f mm rejected (estimate %.1f mm)\n", mean, kfX);
    return false;
  }

  distance_mm = (uint16_t)(kfX + 0.5f);
  float level = constrain(HEIGHT_TANK_MM - kfX, 0.0f, HEIGHT_TANK_MM);
  Serial.printf("[SENSOR] n=%u kept=%u median=%u mean=%.1f filtered=%.1f level=%.1f mm"
                " | noise=%.1f mm (set CHANGE_THRESH >= %.0f mm)\n",
                n, kept, median, mean, kfX, level, sqrtf(noiseVar), 4.0f * sqrtf(noiseVar));

  blindActive            = false;
  lastValidWaterLevel_mm = level;
  lastValidTime          = millis();
  setWaterLevel(level);
  return true;
}

void resetSensorUART() {
  Serial.println("[UART] Sensor timeout → offline, resetting UART...");
  sensorOnline = false;
  sensorSerial.end();
  delay(200);
  sensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  sampleCount      = 0;
  framesReceived   = 0;
  kfInit           = false;
  lastSensorMillis = millis();  // retry again after another SENSOR_TIMEOUT
}

bool simulateWaterSensor() {
  if (manualOverride) return false;
  static float          sim_mm  = 2000.0f;
  static bool           turun   = true;
  static unsigned long  lastSim = 0;

  if (millis() - lastSim < 5000) return false;
  lastSim = millis();

  if (turun) { sim_mm -= 150.0f; if (sim_mm <= 0)             turun = false; }
  else       { sim_mm += 150.0f; if (sim_mm > HEIGHT_TANK_MM) turun = true;  }

  sim_mm           = constrain(sim_mm, 0.0f, HEIGHT_TANK_MM);
  waterLevel_mm    = sim_mm;
  waterLevel_cm    = sim_mm / 10.0f;
  waterLevel_m     = sim_mm / 1000.0f;
  distance_mm      = (uint16_t)(HEIGHT_TANK_MM - sim_mm);
  blindActive      = (distance_mm <= SENSOR_FULL_DIST_MM);
  sensorOnline     = true;
  lastSensorMillis = millis();
  return true;
}

void handleSerialWaterLevel() {
  if (!Serial.available()) return;
  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;
  if (handleConfigCommand(input)) return;

  float val = input.toFloat();
  if (val <= 0 || val > HEIGHT_TANK_M) {
    Serial.printf("[CFG] Water level invalid: \"%s\" (expect 0-%.2f m, or: info | portal | id | wifi | reset)\n",
                  input.c_str(), HEIGHT_TANK_M);
    return;
  }
  manualOverride = true;
  waterLevel_m  = val;
  waterLevel_cm = waterLevel_m * 100.0f;
  waterLevel_mm = waterLevel_m * 1000.0f;
  Serial.printf("[CFG] Water level updated: %.2f m | %.2f cm | %.0f mm\n",
                waterLevel_m, waterLevel_cm, waterLevel_mm);
  updatePumpLogic(true);
}

bool isTankFullByBlind() {
  return sensorOnline && blindActive;
}
