#include "pump.h"
#include "globals.h"

// Dry-run protection: relay ON = pump allowed, relay OFF = pump cut.
// Cut immediately when water is low; re-enable only after the level has
// stayed at/above WARNING for PUMP_CONFIRM_COUNT consecutive updates.
void updatePumpLogic(bool immediate) {
  if (!ENABLE_PUMP) return;
  static uint8_t confirmCount = 0;

  bool newPumpState = pumpState;
  if (!sensorOnline)                          newPumpState = false;  // level unknown → fail-safe
  else if (waterLevel_mm <= EMPTY_LEVEL_MM)   newPumpState = false;  // low water → cut
  else if (waterLevel_mm >= WARNING_LEVEL_MM) newPumpState = true;   // recovered (incl. blind/full)

  if (newPumpState == pumpState) {
    confirmCount = 0;
    return;
  }

  if (newPumpState && !immediate && ++confirmCount < PUMP_CONFIRM_COUNT) return;

  confirmCount = 0;
  pumpState    = newPumpState;
  digitalWrite(RELAY_PIN, pumpState ? RELAY_ON : RELAY_OFF);
  Serial.printf("[PUMP] Relay %s\n", pumpState ? "ON (pump allowed)" : "CUT (water low / sensor offline)");
}
