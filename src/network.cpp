#include "network.h"
#include "globals.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

void connectWiFi() {
  static unsigned long lastAttempt = 0;
  const unsigned long RETRY_INTERVAL = 5000; // Retry every 5 seconds

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected successfully.");
    return; // Already connected
  }

  unsigned long now = millis();
  if (now - lastAttempt >= RETRY_INTERVAL) {
    Serial.println("[WIFI] Attempting to connect...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastAttempt = now;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected successfully.");
  } else {
    Serial.println("[WIFI] Connection failed. Retrying...");
  }
}

void reconnectMQTT() {
  if (!ENABLE_MQTT)                   return;
  if (WiFi.status() != WL_CONNECTED)  return;  // WiFi must be up first

  static bool          wasConnected = false;
  static unsigned long lastAttempt  = 0;
  const  unsigned long COOLDOWN_MS  = 5000;

  bool nowConnected = client.connected();

  // Detect a dropped connection and log it once
  if (wasConnected && !nowConnected) {
    Serial.println("[MQTT] Connection lost");
    wasConnected = false;
  }

  if (nowConnected) {
    wasConnected = true;
    return;
  }

  // Throttle reconnect attempts
  if (millis() - lastAttempt < COOLDOWN_MS) return;
  lastAttempt = millis();

  Serial.print("[MQTT] Connecting...");
  if (client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
    Serial.println(" Connected");
    wasConnected = true;
  } else {
    Serial.printf(" FAIL | state=%d\n", client.state());
  }
}

void initNTP() {
  configTime(7 * 3600, 0, "pool.ntp.org");
}

void updateConfigFromServer() {
  if (!ENABLE_SERVER_CFG) return;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[CONFIG] WiFi not connected");
    return;
  }

  HTTPClient http;
  Serial.println("[CONFIG] Requesting server...");
  http.begin(secured_client, CONFIG_URL);
  int httpCode = http.GET();
  Serial.printf("[CONFIG] HTTP CODE = %d\n", httpCode);
  if (httpCode != 200) { http.end(); return; }

  String payload = http.getString();
  Serial.println("[CONFIG] RAW JSON:");
  Serial.println(payload);

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("[CONFIG] JSON ERROR: %s\n", error.c_str());
    http.end();
    return;
  }

  float newHeight = doc["data"]["height_max"];
  float newWarn   = doc["data"]["height_warning"];
  float newEmpty  = doc["data"]["height_min"];
  Serial.printf("[CONFIG] Parsed → %.3f | %.3f | %.3f\n", newHeight, newWarn, newEmpty);

  if (newHeight <= 0) {
    Serial.println("[CONFIG] height_max INVALID");
    http.end();
    return;
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
}

void publishMQTT() {
  if (!sensorOnline)       return;
  waterLevel_m = waterLevel_mm / 1000.0f;
  if (abs(waterLevel_m - lastWaterLevel_m) < CHANGE_THRESH) return;
  if(!client.connected()) {
    reconnectMQTT();
    if (client.connected()) client.loop();
  }
  char payload[16];
  snprintf(payload, sizeof(payload), "%.2f", waterLevel_m);
  bool ok = client.publish(MQTT_TOPIC, payload, false);
  Serial.printf("[MQTT] Publish: %s → %s\n", payload, ok ? "OK" : "FAIL");
  lastWaterLevel_m = waterLevel_m;

  digitalWrite(LED_IND, LOW);
  delay(200);
  digitalWrite(LED_IND, HIGH);
}
