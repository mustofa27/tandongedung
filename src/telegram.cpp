#include "telegram.h"
#include "globals.h"

void sendTelegram(String msg) {
  for (int i = 0; i < SUP_ADM_COUNT; i++)
    bot.sendMessage(String(id_supAdm[i]), msg, "");
}

void sendTelegramSafe(String msg) {
  if (millis() - lastTgSend > MSG_INTERVAL) {
    sendTelegram(msg);
    lastTgSend = millis();
    Serial.println("[TG] Sent: " + msg);
  }
}
