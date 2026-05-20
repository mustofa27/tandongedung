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
====================================================== */
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "globals.h"
#include "network.h"
#include "sensor.h"
#include "pump.h"
#include "telegram.h"

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(SOFTWARE_VERSION);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_IND, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(LED_IND, HIGH);

  sensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  connectWiFi();
  secured_client.setInsecure();
  initNTP();

  if (ENABLE_SERVER_CFG) updateConfigFromServer();

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
  connectWiFi();

  if (ENABLE_MQTT) {
    reconnectMQTT();
    client.loop();
  }

  if (ENABLE_SENSOR_SIM) {
    simulateWaterSensor();
    updatePumpLogic();
  } else if (ENABLE_SENSOR_UART) {
    if (readWaterSensor(30)) updatePumpLogic();
  }

  if (ENABLE_SERVER_CFG && millis() - configTimer >= CONFIG_INTERVAL) {
    updateConfigFromServer();
    configTimer = millis();
  }

  if (ENABLE_SENSOR_UART && millis() - lastSensorMillis > SENSOR_TIMEOUT) {
    resetSensorUART();
  }

  handleSerialWaterLevel();

  if (millis() - mqttTimer >= MQTT_INTERVAL) {
    if (ENABLE_MQTT) publishMQTT();
    mqttTimer = millis();
  }

  static unsigned long lastBotPoll = 0;
  if (millis() - lastBotPoll > TG_POLL_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED)
      bot.getUpdates(bot.last_message_received + 1);
    lastBotPoll = millis();
  }

  if (millis() - hbTimer >= HB_INTERVAL) {
    Serial.printf(" | HEIGHT=%.2f m | [HB] %.2f mm | %.2f cm | %.2f m"
                  " | dist=%u mm | Pump=%s%s%s\n",
                  HEIGHT_TANK_M,
                  waterLevel_mm, waterLevel_cm, waterLevel_m,
                  distance_mm,
                  pumpState    ? "ON"               : "OFF",
                  blindActive  ? " | BLIND"          : "",
                  !sensorOnline ? " | SENSOR OFFLINE" : "");
    hbTimer = millis();
  }
}

// ---- Everything below this line has been moved to separate files ----
// network.cpp  → connectWiFi, reconnectMQTT, initNTP, updateConfigFromServer, publishMQTT
// sensor.cpp   → readSingleSensorValue, readWaterSensor, resetSensorUART,
//                simulateWaterSensor, handleSerialWaterLevel, isTankFullByBlind
// pump.cpp     → updatePumpLogic
// telegram.cpp → sendTelegram, sendTelegramSafe
// globals.cpp  → all shared global variables
// config.h     → all compile-time constants and feature flags

