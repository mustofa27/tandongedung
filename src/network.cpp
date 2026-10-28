#include "network.h"
#include "globals.h"
#include "device_config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Non-blocking: starts/retries the connection at most every WIFI_RETRY_INTERVAL
void connectWiFi() {
  static bool          started   = false;
  static bool          wasOnline = false;
  static unsigned long lastTry   = 0;

  if (WiFi.status() == WL_CONNECTED) {
    if (!wasOnline) Serial.println("[WIFI] Connected");
    wasOnline = true;
    return;
  }
  if (wasOnline) Serial.println("[WIFI] Disconnected");
  wasOnline = false;

  // While the portal is open, keep retrying STA unless someone is using the portal
  if (isConfigPortalActive() && WiFi.softAPgetStationNum() > 0) return;
  if (started && millis() - lastTry < WIFI_RETRY_INTERVAL) return;
  lastTry = millis();
  if (!hasSavedWiFi()) return;  // nothing to join until set via portal / serial
  if (!started) {
    if (!isConfigPortalActive()) WiFi.mode(WIFI_STA);  // don't drop the portal AP
    WiFi.begin();                                      // stored credentials
  } else {
    WiFi.reconnect();
  }
  started = true;
  Serial.println("[WIFI] Connecting...");
}

// Boot only: give WiFi a bounded time so the first server config can load
void waitForWiFi(unsigned long timeoutMs) {
  connectWiFi();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) startConfigPortal();  // e.g. moved to a new site
}

// Non-blocking: one connect attempt at most every MQTT_RETRY_INTERVAL
void reconnectMQTT() {
  if (!ENABLE_MQTT)                   return;
  if (client.connected())             return;
  if (WiFi.status() != WL_CONNECTED)  return;
  if (deviceId.length() == 0)         return;  // not configured yet

  static unsigned long lastTry = 0;
  if (lastTry != 0 && millis() - lastTry < MQTT_RETRY_INTERVAL) return;
  lastTry = millis();

  Serial.print("[MQTT] Connecting...");
  if (client.connect(deviceId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
    Serial.println(" connected");
  } else {
    Serial.printf(" FAIL | state=%d\n", client.state());
  }
}

void initNTP() {
  configTime(7 * 3600, 0, "pool.ntp.org");
}

bool updateConfigFromServer() {
  if (!ENABLE_SERVER_CFG)      return false;
  if (deviceId.length() == 0)  return false;  // not configured yet
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[CONFIG] WiFi not connected");
    return false;
  }

  HTTPClient http;
  Serial.println("[CONFIG] Requesting server...");
  http.begin(secured_client, configUrl);
  int httpCode = http.GET();
  Serial.printf("[CONFIG] HTTP CODE = %d\n", httpCode);
  if (httpCode != 200) { http.end(); return false; }

  String payload = http.getString();
  Serial.println("[CONFIG] RAW JSON:");
  Serial.println(payload);

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("[CONFIG] JSON ERROR: %s\n", error.c_str());
    http.end();
    return false;
  }

  float newHeight = doc["data"]["height_max"];
  float newWarn   = doc["data"]["height_warning"];
  float newEmpty  = doc["data"]["height_min"];
  Serial.printf("[CONFIG] Parsed → %.3f | %.3f | %.3f\n", newHeight, newWarn, newEmpty);

  if (newHeight <= 0) {
    Serial.println("[CONFIG] height_max INVALID");
    http.end();
    return false;
  }

  HEIGHT_TANK_M    = newHeight;
  WARNING_LEVEL_M  = newWarn;
  EMPTY_LEVEL_M    = newEmpty;
  HEIGHT_TANK_MM   = HEIGHT_TANK_M   * 1000.0f;
  WARNING_LEVEL_MM = WARNING_LEVEL_M * 1000.0f;
  EMPTY_LEVEL_MM   = EMPTY_LEVEL_M   * 1000.0f;

  Serial.println("=== CONFIG UPDATED ===");
  Serial.printf("HEIGHT_TANK_M = %.3f\n", HEIGHT_TANK_M);
  http.end();
  return true;
}

void publishMQTT() {
  if (!ENABLE_MQTT)        return;
  if (!sensorOnline)       return;

  waterLevel_m = waterLevel_mm / 1000.0f;
  if (abs(waterLevel_m - lastWaterLevel_m) < CHANGE_THRESH) return;
  if (!client.connected())  return;  // retried in loop(); publishes once reconnected

  char payload[16];
  snprintf(payload, sizeof(payload), "%.3f", waterLevel_m);  // mm resolution
  bool ok = client.publish(mqttTopic.c_str(), payload, false);
  Serial.printf("[MQTT] Publish: %s → %s\n", payload, ok ? "OK" : "FAIL");
  lastWaterLevel_m = waterLevel_m;

  digitalWrite(LED_IND, LOW);
  delay(200);
  digitalWrite(LED_IND, HIGH);
}
