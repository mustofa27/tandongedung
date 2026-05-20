#pragma once

/* ================= SOFTWARE ================= */
#define SOFTWARE_VERSION "TANDON GEDUNG A"

/* ================= WIFI ================= */
#define WIFI_SSID     "POLITEKNIK-NEGERI-MADURA"
#define WIFI_PASSWORD ""

/* ================= MQTT ================= */
#define MQTT_SERVER    "mqtt.icminovasi.my.id"
#define MQTT_PORT      8883
#define MQTT_USER      "poltera_water"
#define MQTT_PASSWORD  "P0lterajaya@2026"
#define MQTT_CLIENT_ID "bengkel_tenaga-8f14"
#define MQTT_TOPIC     "poltera_water/bengkel_tenaga-8f14/water_level"

/* ================= HTTP CONFIG ================= */
#define CONFIG_URL \
  "https://watermonitoring.poltera.ac.id/api/tandons/bengkel_tenaga-8f14"

/* ================= PINS ================= */
#define SENSOR_RX_PIN 16
#define SENSOR_TX_PIN 17
#define RELAY_PIN     27
#define LED_IND       19

/* ================= RELAY ================= */
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

/* ================= TANDON ================= */
#define CHANGE_THRESH     0.005f   // 5 mm change threshold
#define BLIND_DISTANCE_MM 300.0f   // 30 cm sensor blind zone

/* ================= TIMING (ms) ================= */
#define MQTT_INTERVAL    1000
#define HB_INTERVAL      200
#define MSG_INTERVAL     3000
#define SENSOR_TIMEOUT   15000   // raised: readWaterSensor(15) worst-case ~8 s
#define CONFIG_INTERVAL  600000  // 10 minutes
#define TG_POLL_INTERVAL 10000

/* ================= TELEGRAM ================= */
#define BOT_TOKEN      ""
#define SUP_ADM_COUNT  2

/* ================= FEATURE FLAGS ================= */
#define ENABLE_SENSOR_SIM  false
#define ENABLE_SENSOR_UART true
#define ENABLE_PUMP        true
#define ENABLE_SERVER_CFG  true
#define ENABLE_MQTT        true
