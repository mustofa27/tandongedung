#include "network.h"
#include "globals.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\n[WIFI] Connected");
}

void reconnectMQTT() {
  if (!ENABLE_MQTT)      return;
  if (client.connected()) return;
  while (!client.connected()) {
    Serial.print("[MQTT] Connecting...");
    if (client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("[MQTT] Connected");
    } else {
      Serial.print("FAIL | state=");
      Serial.println(client.state());
      delay(2000);
    }
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
  if (!ENABLE_MQTT)        return;
  if (!sensorOnline)       return;
  if (!client.connected()) return;

  waterLevel_m = waterLevel_mm / 1000.0f;
  if (abs(waterLevel_m - lastWaterLevel_m) < CHANGE_THRESH) return;

  char payload[16];
  snprintf(payload, sizeof(payload), "%.2f", waterLevel_m);
  bool ok = client.publish(MQTT_TOPIC, payload, false);
  Serial.printf("[MQTT] Publish: %s → %s\n", payload, ok ? "OK" : "FAIL");
  lastWaterLevel_m = waterLevel_m;

  digitalWrite(LED_IND, LOW);
  delay(200);
  digitalWrite(LED_IND, HIGH);
}
