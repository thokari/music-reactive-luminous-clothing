#include "PushButtons.h"

namespace {
  struct State {
    uint8_t pin = 0;
    uint32_t debounceMs = 5;
    uint32_t pauseMs = 1000;
    volatile bool pressed = false;
    volatile uint32_t lastPressTime = 0;
    volatile uint32_t lastEdgeTime = 0;
  } s;

  void IRAM_ATTR isrFalling() {
    uint32_t now = millis();
    if (now - s.lastEdgeTime > s.debounceMs) {
      s.pressed = true;
      s.lastPressTime = now;
    }
    s.lastEdgeTime = now;
  }

  void IRAM_ATTR isrRising() {
    s.lastEdgeTime = millis();
  }
}

void pushButtonsBegin(uint8_t pin, uint32_t debounceMs, uint32_t pauseMs) {
  s.pin = pin;
  s.debounceMs = debounceMs;
  s.pauseMs = pauseMs;
  pinMode(s.pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(s.pin), isrFalling, FALLING);
  attachInterrupt(digitalPinToInterrupt(s.pin), isrRising, RISING);
}

void pushButtonsUpdate(uint32_t) {
}

bool pushButtonsShouldSkipLoop() {
  uint32_t now = millis();
  return (now - s.lastPressTime) <= s.pauseMs;
}

bool pushButtonConsumePressed() {
  if (s.pressed) {
    s.pressed = false;
    return true;
  }
  return false;
}

uint32_t pushButtonLastPressTime() {
  return s.lastPressTime;
}
