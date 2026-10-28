#pragma once

void connectWiFi();
void waitForWiFi(unsigned long timeoutMs);
void reconnectMQTT();
void initNTP();
bool updateConfigFromServer();  // true when config loaded
void publishMQTT();
