#include "ModeRegistry.h"
#include "Modes.h"

static void onEnterNoop() {}

const Mode modes[] = {
  { "reactivePulse", ModeType::Reactive, reactivePulse, nullptr },
  { "reactivePulseDecay", ModeType::Reactive, reactivePulseWithDecay, nullptr },
  { "reactiveRandom", ModeType::Reactive, reactiveRandomSimple, nullptr },
  { "reactiveRandomHL", ModeType::Reactive, reactiveRandomHighLow, nullptr },
  { "periodicPulseUp", ModeType::Periodic, periodicPulseUp, nullptr },
  { "periodicPulseUpDown", ModeType::Periodic, periodicPulseUpDown, nullptr },
  { "periodicFlash", ModeType::Periodic, periodicFlash, nullptr },
  { "periodicFlashDecay", ModeType::Periodic, periodicFlashWithDecay, nullptr },
  { "periodicRandom", ModeType::Periodic, periodicRandom, nullptr },
};

uint8_t getModeCount() {
  return (uint8_t)(sizeof(modes) / sizeof(modes[0]));
}

bool isReactive(uint8_t idx) {
  if (idx >= getModeCount()) return false;
  return modes[idx].type == ModeType::Reactive;
}
