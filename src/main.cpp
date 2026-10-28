/* ======================================================
   TANDON OTOMATIS — MONITORING & CONTROL
   MQTT PUBLISH + HTTPS CONFIG (SERVER KAMPUS) — GEDUNG A

   CHANGELOG:
   [FIX-1] Remove erroneous "+ BLIND_DISTANCE_MM" offset
   [FIX-2] Remove checkBlindSensor() from loop()
           → blindActive is managed inside readWaterSensor()
   [FIX-3] Hold-last-valid: add sensorOnline=true & lastSensorMillis
   [FIX-4] Handle all-samples-rejected case (blind zone total)
   [FIX-5] SENSOR_TIMEOUT raised to 15000 ms
   [FIX-6] Remove dead code before blind zone check
   [CHG-7] Non-blocking sensor read, median + Kalman filter
   [CHG-8] Relay = dry-run protection: cut on low water / sensor offline
   [CHG-9] Non-blocking WiFi/MQTT reconnect, client.loop() every pass
   [CHG-10] Setup portal + serial commands: WiFi & device id stored in NVS
====================================================== */
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "globals.h"
#include "network.h"
#include "sensor.h"
#include "pump.h"
#include "telegram.h"
#include "device_config.h"

static bool configLoaded = false;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(SOFTWARE_VERSION);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_IND, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(LED_IND, HIGH);
  ResetPress press = checkResetButton();  // as early as possible after boot

  sensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  checkFirmwareUpdate();  // new upload → config cleared → portal below
  loadDeviceConfig();
  if (press == RESET_NONE && isProvisioned()) {
    waitForWiFi(15000);
  } else {
    startConfigPortal();
    connectWiFi();  // saved WiFi still connects in the background
  }
  secured_client.setInsecure();
  mqtt_secure_client.setInsecure();
  initNTP();

  if (ENABLE_SERVER_CFG) configLoaded = updateConfigFromServer();

  if (ENABLE_MQTT) {
    client.setServer(MQTT_SERVER, MQTT_PORT);
    client.setKeepAlive(60);
    client.setSocketTimeout(5);
  }

  WARNING_LEVEL_MM = WARNING_LEVEL_M * 1000.0f;
  EMPTY_LEVEL_MM   = EMPTY_LEVEL_M   * 1000.0f;
  Serial.println("[SYSTEM] Ready!");
}

void loop() {
  handleConfigPortal();
  connectWiFi();

  if (ENABLE_MQTT) {
    if (client.connected()) client.loop();
    else                    reconnectMQTT();
  }

  if (ENABLE_SENSOR_SIM) {
    if (simulateWaterSensor()) updatePumpLogic();
  } else if (ENABLE_SENSOR_UART) {
    if (readWaterSensor()) updatePumpLogic();
  }

  // Retry every 30 s until the first config loads, then every CONFIG_INTERVAL
  unsigned long cfgInterval = configLoaded ? CONFIG_INTERVAL : 30000;
  if (ENABLE_SERVER_CFG && WiFi.status() == WL_CONNECTED &&
      millis() - configTimer >= cfgInterval) {
    if (updateConfigFromServer()) configLoaded = true;
    configTimer = millis();
  }

  if (ENABLE_SENSOR_UART && millis() - lastSensorMillis > SENSOR_TIMEOUT) {
    resetSensorUART();
    updatePumpLogic();  // sensor offline → cut relay
  }

  handleSerialWaterLevel();

  if (millis() - mqttTimer >= MQTT_INTERVAL) {
    if (ENABLE_MQTT) publishMQTT();
    mqttTimer = millis();
  }

  static unsigned long lastBotPoll = 0;
  if (millis() - lastBotPoll > TG_POLL_INTERVAL) {
    if (strlen(BOT_TOKEN) > 0 && WiFi.status() == WL_CONNECTED)
      bot.getUpdates(bot.last_message_received + 1);
    lastBotPoll = millis();
  }

  if (millis() - hbTimer >= HB_INTERVAL) {
    Serial.printf(" | HEIGHT=%.2f m | [HB] %.2f mm | %.2f cm | %.2f m"
                  " | dist=%u mm | Relay=%s%s%s\n",
                  HEIGHT_TANK_M,
                  waterLevel_mm, waterLevel_cm, waterLevel_m,
                  distance_mm,
                  pumpState    ? "ON"               : "CUT",
                  blindActive  ? " | BLIND"          : "",
                  !sensorOnline ? " | SENSOR OFFLINE" : "");
    hbTimer = millis();
  }
}

// ---- Everything below this line has been moved to separate files ----
// network.cpp  → connectWiFi, reconnectMQTT, initNTP, updateConfigFromServer, publishMQTT
// sensor.cpp   → readWaterSensor, resetSensorUART,
//                simulateWaterSensor, handleSerialWaterLevel, isTankFullByBlind
// pump.cpp     → updatePumpLogic
// telegram.cpp → sendTelegram, sendTelegramSafe
// device_config.cpp → device id + WiFi setup portal, serial config commands
// globals.cpp  → all shared global variables
// config.h     → all compile-time constants and feature flags

