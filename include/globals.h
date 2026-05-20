#pragma once

#include <Arduino.h>
#include "config.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <UniversalTelegramBot.h>

/* ---- Network objects ---- */
extern WiFiClientSecure     secured_client;
extern PubSubClient         client;
extern UniversalTelegramBot bot;

/* ---- Sensor hardware ---- */
extern HardwareSerial sensorSerial;
extern uint16_t       distance_mm;

/* ---- Water levels ---- */
extern float waterLevel_mm;
extern float waterLevel_cm;
extern float waterLevel_m;
extern float lastWaterLevel_m;

/* ---- Tank config (may be updated from server) ---- */
extern float EMPTY_LEVEL_M;
extern float WARNING_LEVEL_M;
extern float HEIGHT_TANK_M;
extern float HEIGHT_TANK_MM;
extern float WARNING_LEVEL_MM;
extern float EMPTY_LEVEL_MM;

/* ---- State flags ---- */
extern bool pumpState;
extern bool sensorOnline;
extern bool blindActive;
extern bool manualOverride;

/* ---- Timers ---- */
extern uint32_t      mqttTimer;
extern uint32_t      hbTimer;
extern uint32_t      configTimer;
extern unsigned long lastSensorMillis;

/* ---- Telegram recipients ---- */
extern long          id_supAdm[SUP_ADM_COUNT];
extern unsigned long lastTgSend;
