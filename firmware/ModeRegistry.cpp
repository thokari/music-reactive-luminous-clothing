#include "ModeRegistry.h"
#include "Modes.h"

static void onEnterNoop() {}

const Mode modes[] = {
  { "rPulse", ModeType::Reactive, reactivePulse, nullptr },
  { "rPulseDecay", ModeType::Reactive, reactivePulseWithDecay, nullptr },
  { "rRand", ModeType::Reactive, reactiveRandomSimple, nullptr },
  { "rRandSwap", ModeType::Reactive, reactiveRandomSwap, nullptr },
  { "fPulseUp", ModeType::Fixed, fixedPulseUp, nullptr },
  { "fPulseUpDown", ModeType::Fixed, fixedPulseUpDown, nullptr },
  { "fFlash", ModeType::Fixed, fixedFlash, nullptr },
  { "fFlashDecay", ModeType::Fixed, fixedFlashWithDecay, nullptr },
  { "fRandom", ModeType::Fixed, fixedRandom, nullptr },
};

uint8_t getModeCount() {
  return (uint8_t)(sizeof(modes) / sizeof(modes[0]));
}

bool isReactive(uint8_t idx) {
  if (idx >= getModeCount()) return false;
  return modes[idx].type == ModeType::Reactive;
}
