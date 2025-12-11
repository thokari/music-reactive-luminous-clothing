#ifndef MODE_REGISTRY_H
#define MODE_REGISTRY_H
#include "Arduino.h"

enum class ModeType : uint8_t {
  Reactive = 0,
  Periodic = 1
};

struct Mode {
  const char* label;
  ModeType type;
  void (*run)();
  void (*onEnter)();
};

extern const Mode modes[];
uint8_t getModeCount();
bool isReactive(uint8_t idx);

#endif // MODE_REGISTRY_H
