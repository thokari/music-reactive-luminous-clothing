#ifndef PUSH_BUTTONS_H
#define PUSH_BUTTONS_H

#include "Arduino.h"

void pushButtonsBegin(uint8_t pin, uint32_t debounceMs, uint32_t pauseMs);
void pushButtonsUpdate(uint32_t nowMs);
bool pushButtonsShouldSkipLoop();
bool pushButtonConsumePressed();
uint32_t pushButtonLastPressTime();

#endif // PUSH_BUTTONS_H
