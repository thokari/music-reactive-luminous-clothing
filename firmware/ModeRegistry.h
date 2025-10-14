#ifndef MODE_REGISTRY_H
#define MODE_REGISTRY_H

#include "Arduino.h"

enum class ModeType : uint8_t {
  Reactive = 0,
  Periodic = 1
};

struct Mode {
  const char* label; // e.g., "R1", "F1"
  ModeType type;
  void (*run)();
  void (*onEnter)(); // optional hook when entering mode (can be nullptr)
};

extern const Mode modes[];
uint8_t getModeCount();
bool isReactive(uint8_t idx);

#endif // MODE_REGISTRY_H
