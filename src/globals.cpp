#include "globals.h"

WiFiClientSecure     secured_client;
PubSubClient         client(secured_client);
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

HardwareSerial sensorSerial(1);

uint16_t distance_mm     = 0;
float    waterLevel_mm   = 0.0f;
float    waterLevel_cm   = 0.0f;
float    waterLevel_m    = 0.0f;
float    lastWaterLevel_m = -999.0f;

float EMPTY_LEVEL_M    = 0.15f;
float WARNING_LEVEL_M  = 0.40f;
float HEIGHT_TANK_M    = 1.02f;
float HEIGHT_TANK_MM   = 1025.0f;
float WARNING_LEVEL_MM = 0.0f;
float EMPTY_LEVEL_MM   = 0.0f;

bool pumpState      = false;
bool sensorOnline   = false;
bool blindActive    = false;
bool manualOverride = false;

uint32_t      mqttTimer        = 0;
uint32_t      hbTimer          = 0;
uint32_t      configTimer      = 0;
unsigned long lastSensorMillis = 0;

long          id_supAdm[SUP_ADM_COUNT] = { 967791356, 689884161 };
unsigned long lastTgSend               = 0;
