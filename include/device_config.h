#pragma once

#include <Arduino.h>

/* ---- Runtime identity (stored in NVS, defaults from config.h) ---- */
extern String deviceId;
extern String mqttTopic;
extern String configUrl;

enum ResetPress { RESET_NONE, RESET_SINGLE, RESET_DOUBLE };
ResetPress checkResetButton();  // call before loadDeviceConfig(); DOUBLE → config was reset
void checkFirmwareUpdate();  // new firmware since last boot → clear WiFi + device id
void loadDeviceConfig();
bool hasSavedWiFi();
bool isProvisioned();        // WiFi and device id both set

/* ---- Setup portal (WiFi + device id), non-blocking ---- */
void startConfigPortal();
void handleConfigPortal();  // call every loop: BOOT button, portal, pending restart
bool isConfigPortalActive();

/* ---- Serial commands: portal | info | id <id> | wifi <ssid>,<pass> | reset ---- */
bool handleConfigCommand(const String& cmd);
