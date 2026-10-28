#include "device_config.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

String deviceId;
String mqttTopic;
String configUrl;

static WiFiManager           wm;
static WiFiManagerParameter* idParam        = nullptr;
static bool                  restartPending = false;
static unsigned long         restartAt      = 0;

/* ===== Internal helpers ===== */

static bool isValidDeviceId(const String& id) {
  if (id.length() == 0 || id.length() > DEVICE_ID_MAX_LEN) return false;
  for (char c : id)
    if (!isalnum(c) && c != '_' && c != '-') return false;
  return true;
}

static void applyDeviceId(const String& id) {
  deviceId  = id;
  mqttTopic = id.length() ? String(MQTT_TOPIC_PREFIX) + id + MQTT_TOPIC_SUFFIX : "";
  configUrl = id.length() ? String(CONFIG_URL_BASE) + id : "";
}

static void saveDeviceId(const String& id) {
  Preferences prefs;
  prefs.begin("tandon", false);
  prefs.putString("device_id", id);
  prefs.end();
  Serial.printf("[DEVICE] New device id saved: %s\n", id.c_str());
}

// Restart shortly after saving so the portal can still send its response page
static void scheduleRestart() {
  restartPending = true;
  restartAt      = millis() + 2000;
}

static void onPortalSave() {
  String id = String(idParam->getValue());
  id.trim();
  if (!isValidDeviceId(id))  Serial.println("[PORTAL] Invalid device id, keeping current");
  else if (id != deviceId)   saveDeviceId(id);
  scheduleRestart();
}

// Clear saved device id + WiFi → portal must be completed again
static void factoryReset() {
  Preferences prefs;
  prefs.begin("tandon", false);
  prefs.clear();
  prefs.end();
  wm.resetSettings();
  Serial.println("[DEVICE] Config cleared (WiFi + device id)");
}

// Blink LED fast while the window is open, then disarm the double-reset flag
static void disarmResetTask(void*) {
  for (uint32_t t = 0; t < DRD_WINDOW_MS; t += 100) {
    digitalWrite(LED_IND, (t / 100) % 2 ? HIGH : LOW);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  digitalWrite(LED_IND, HIGH);
  Serial.println("[DEVICE] Double-reset window closed");
  Preferences prefs;
  prefs.begin("drd", false);
  prefs.putBool("armed", false);
  prefs.end();
  vTaskDelete(nullptr);
}

/* ===== Public functions ===== */

// Reset button (EN): once → portal, twice → config reset + portal. The button
// cuts power to the chip, so presses are counted as power-on boots via a flag in NVS.
ResetPress checkResetButton() {
  esp_reset_reason_t reason  = esp_reset_reason();
  bool               powerOn = reason == ESP_RST_POWERON || reason == ESP_RST_EXT;  // EN button / power cycle

  Preferences prefs;
  prefs.begin("drd", false);
  bool armed = prefs.getBool("armed", false);
  prefs.putBool("armed", powerOn && !armed);
  prefs.end();
  Serial.printf("[DEVICE] Reset reason=%d (%s), double-reset armed=%s\n",
                reason, powerOn ? "button/power" : "software", armed ? "yes" : "no");

  if (!powerOn) return RESET_NONE;

  if (armed) {
    Serial.println("[DEVICE] Reset button pressed twice → reset config, open portal");
    factoryReset();
    return RESET_DOUBLE;
  }
  Serial.printf("[DEVICE] Reset button pressed → open portal "
                "(press again within %d s while LED blinks to reset config)\n",
                DRD_WINDOW_MS / 1000);
  xTaskCreate(disarmResetTask, "drd", 3072, nullptr, 1, nullptr);
  return RESET_SINGLE;
}

// Compare the running firmware's ELF hash with the one seen last boot
void checkFirmwareUpdate() {
  char sha[17] = { 0 };
  esp_ota_get_app_elf_sha256(sha, sizeof(sha));

  Preferences prefs;
  prefs.begin("fw", false);
  String last = prefs.getString("sha", "");
  if (last != sha) {
    Serial.printf("[DEVICE] Firmware changed (%s → %s) → setup required\n",
                  last.length() ? last.c_str() : "none", sha);
    factoryReset();
    prefs.putString("sha", sha);
  }
  prefs.end();
}

void loadDeviceConfig() {
  Preferences prefs;
  prefs.begin("tandon", false);
  String id = prefs.getString("device_id", "");
  prefs.end();

  applyDeviceId(isValidDeviceId(id) ? id : "");
  pinMode(PORTAL_BUTTON_PIN, INPUT_PULLUP);
  if (isProvisioned()) Serial.printf("[DEVICE] id=%s | topic=%s\n", deviceId.c_str(), mqttTopic.c_str());
  else                 Serial.println("[DEVICE] Not configured → setup portal required");
}

bool hasSavedWiFi() {
  if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);  // stored config needs the driver up
  return wm.getWiFiIsSaved();
}

bool isProvisioned() {
  return deviceId.length() > 0 && hasSavedWiFi();
}

void startConfigPortal() {
  if (wm.getConfigPortalActive()) return;

  if (!idParam) {
    idParam = new WiFiManagerParameter("device_id", "Device ID (a-z 0-9 _ -)",
                                       deviceId.c_str(), DEVICE_ID_MAX_LEN);
    wm.addParameter(idParam);
    static std::vector<const char*> menu = { "wifi", "param", "info", "exit" };
    wm.setMenu(menu);
    wm.setConfigPortalBlocking(false);
    wm.setBreakAfterConfig(true);           // save even if the new WiFi fails to connect
    wm.setSaveConfigCallback(onPortalSave); // "Configure WiFi" page
    wm.setSaveParamsCallback(onPortalSave); // "Setup" page (device id only)
  }
  // Unconfigured: stay open until setup is done
  wm.setConfigPortalTimeout(isProvisioned() ? PORTAL_TIMEOUT_S : 0);

  // Unconfigured boards have no id yet → use the chip MAC suffix so APs stay unique
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
  String ap = String(PORTAL_AP_PREFIX) + (deviceId.length() ? deviceId : String(suffix));
  Serial.printf("[PORTAL] Open: join WiFi \"%s\" (password \"%s\"), then browse 192.168.4.1\n",
                ap.c_str(), PORTAL_AP_PASSWORD);
  wm.startConfigPortal(ap.c_str(), PORTAL_AP_PASSWORD);
}

void handleConfigPortal() {
  // Hold BOOT button → open portal
  static unsigned long pressedAt = 0;
  if (digitalRead(PORTAL_BUTTON_PIN) == LOW) {
    if (pressedAt == 0) pressedAt = millis();
    else if (millis() - pressedAt >= PORTAL_HOLD_MS) {
      pressedAt = 0;
      startConfigPortal();
    }
  } else {
    pressedAt = 0;
  }

  if (wm.getConfigPortalActive()) wm.process();
  else if (!restartPending && !isProvisioned()) startConfigPortal();  // reopen if closed before setup

  if (restartPending && (long)(millis() - restartAt) >= 0) {
    Serial.println("[DEVICE] Config saved → restarting");
    delay(100);
    ESP.restart();
  }
}

bool isConfigPortalActive() {
  return wm.getConfigPortalActive();
}

bool handleConfigCommand(const String& cmd) {
  if (cmd == "portal") {
    startConfigPortal();
    return true;
  }

  if (cmd == "info") {
    Serial.printf("[DEVICE] id=%s\n  WiFi=%s (%s) IP=%s\n  topic=%s\n  config=%s\n",
                  deviceId.c_str(), WiFi.SSID().c_str(),
                  WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                  WiFi.localIP().toString().c_str(), mqttTopic.c_str(), configUrl.c_str());
    return true;
  }

  if (cmd.startsWith("id ")) {
    String id = cmd.substring(3);
    id.trim();
    if (!isValidDeviceId(id)) {
      Serial.println("[DEVICE] Invalid id (1-32 chars: a-z A-Z 0-9 _ -)");
      return true;
    }
    saveDeviceId(id);
    scheduleRestart();
    return true;
  }

  if (cmd.startsWith("wifi ")) {
    String args  = cmd.substring(5);
    int    comma = args.indexOf(',');
    String ssid  = comma < 0 ? args : args.substring(0, comma);
    String pass  = comma < 0 ? ""   : args.substring(comma + 1);
    ssid.trim();
    if (ssid.length() == 0) {
      Serial.println("[WIFI] Usage: wifi <ssid>,<password>");
      return true;
    }
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.c_str());  // persisted to NVS by the WiFi driver
    Serial.printf("[WIFI] New WiFi saved: %s\n", ssid.c_str());
    scheduleRestart();
    return true;
  }

  if (cmd == "reset") {
    factoryReset();
    scheduleRestart();
    return true;
  }

  return false;
}
