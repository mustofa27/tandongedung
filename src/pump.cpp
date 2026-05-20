#include "pump.h"
#include "globals.h"

void updatePumpLogic() {
  if (!ENABLE_PUMP) return;

  bool newPumpState = pumpState;
  if (blindActive)                            newPumpState = false;
  else if (waterLevel_mm <= EMPTY_LEVEL_MM)   newPumpState = true;
  else if (waterLevel_mm >= WARNING_LEVEL_MM) newPumpState = false;

  if (newPumpState != pumpState) {
    pumpState = newPumpState;
    digitalWrite(RELAY_PIN, pumpState ? RELAY_ON : RELAY_OFF);
  }
}
