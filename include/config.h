#pragma once

/* ================= SOFTWARE ================= */
#define SOFTWARE_VERSION "TANDON GEDUNG A"

/* ================= WIFI + DEVICE ID =================
   No built-in defaults: both are set through the setup portal (or serial
   commands) and stored in NVS. Every firmware upload clears them, so a
   freshly flashed board always opens the portal until it is configured. */
#define DEVICE_ID_MAX_LEN 32

/* ================= SETUP PORTAL ================= */
#define PORTAL_AP_PREFIX   "TANDON-"   // AP name = prefix + device id
#define PORTAL_AP_PASSWORD "tandon123" // min 8 chars — change before deploying
#define PORTAL_TIMEOUT_S   300         // portal closes after 5 min without saving (only once configured)
#define PORTAL_BUTTON_PIN  0           // on-board BOOT button
#define PORTAL_HOLD_MS     3000        // hold BOOT this long to open the portal
#define DRD_WINDOW_MS      5000        // 2nd reset-button press within this → reset config + portal

/* ================= MQTT ================= */
#define MQTT_SERVER       "mqtt.icminovasi.my.id"
#define MQTT_PORT         8883
#define MQTT_USER         "poltera_water"
#define MQTT_PASSWORD     "P0lterajaya@2026"
#define MQTT_TOPIC_PREFIX "poltera_water/"  // topic = prefix + device id + suffix
#define MQTT_TOPIC_SUFFIX "/water_level"

/* ================= HTTP CONFIG ================= */
#define CONFIG_URL_BASE "https://watermonitoring.poltera.ac.id/api/tandons/"  // + device id

/* ================= PINS ================= */
#define SENSOR_RX_PIN 16
#define SENSOR_TX_PIN 17
#define RELAY_PIN     27
#define LED_IND       19

/* ================= RELAY ================= */
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

/* ================= TANDON ================= */
#define CHANGE_THRESH     0.010f   // publish deadband (m); keep >= 4x logged noise or consumption over-counts
#define BLIND_DISTANCE_MM 300.0f   // 30 cm sensor blind zone
#define RANGE_MARGIN_MM   200.0f   // accept raw readings up to tank height + 20 cm

/* ================= SENSOR FILTER ================= */
#define SENSOR_UPDATE_INTERVAL 1000  // ms between level updates (median of samples collected)
#define SENSOR_MIN_SAMPLES     3     // fewer valid samples than this → skip update
#define SENSOR_MAX_SAMPLES     30    // sample buffer size per update
#define MAD_GATE               3.0f  // drop samples > this many robust sigmas from the window median
#define MAD_MIN_SIGMA_MM       2.0f  // floor so a very tight window isn't over-trimmed
#define KF_Q                   9.0f  // process noise: level drift per update (mm^2)
#define KF_R                   64.0f // measurement noise: sensor variance (mm^2, ~8 mm std)
#define KF_GATE_SIGMA          3.0f  // reject measurements further than this many sigma
#define KF_MAX_REJECTS         5     // consecutive rejects → accept as real jump, re-init

/* ================= PUMP PROTECTION ================= */
#define PUMP_CONFIRM_COUNT 3  // consecutive updates above WARNING before relay re-enabled

/* ================= TIMING (ms) ================= */
#define MQTT_INTERVAL    1000
#define HB_INTERVAL      200
#define MSG_INTERVAL     3000
#define SENSOR_TIMEOUT   5000    // no valid level for this long → sensor offline, reset UART
#define CONFIG_INTERVAL  600000  // 10 minutes
#define TG_POLL_INTERVAL 10000
#define WIFI_RETRY_INTERVAL 10000
#define MQTT_RETRY_INTERVAL 5000

/* ================= TELEGRAM ================= */
#define BOT_TOKEN      ""
#define SUP_ADM_COUNT  2

/* ================= FEATURE FLAGS ================= */
#define ENABLE_SENSOR_SIM  false
#define ENABLE_SENSOR_UART true
#define ENABLE_PUMP        true
#define ENABLE_SERVER_CFG  true
#define ENABLE_MQTT        true
