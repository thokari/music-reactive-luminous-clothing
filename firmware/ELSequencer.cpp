#include "ELSequencer.h"

ELSequencer::ELSequencer(const uint8_t order[], const uint8_t count)
  : channelOrder(order), channelCount(count) {
    channelIndices = new uint8_t[count];
    for (uint8_t i = 0; i < count; i++) {
      channelIndices[i] = i;
    }
    currentPattern = new uint8_t[count];
    for (uint8_t i = 0; i < count; i++) {
      currentPattern[i] = 0;
    }
  }

void ELSequencer::begin() {
  initSequencer();
  playWireStartSequence();
}

void ELSequencer::lightNumWires(uint8_t num) {
  for (uint8_t i = 0; i < channelCount; i++) {
    uint8_t value = i < num ? HIGH : LOW;
    digitalWrite(channelOrder[i], value);
    currentPattern[i] = (value == HIGH) ? 1 : 0;
  }
}

void ELSequencer::lightWiresAtIndex(uint8_t index) {
  for (uint8_t i = 0; i < channelCount; i++) {
    uint8_t value = i == index ? HIGH : LOW;
    digitalWrite(channelOrder[i], value);
    currentPattern[i] = (value == HIGH) ? 1 : 0;
  }
}

void ELSequencer::lightNumWiresUpToWire(uint8_t num, uint8_t wireNum) {
  for (uint8_t i = 0; i < channelCount; i++) {
    uint8_t value = ((wireNum > i) && (i + num >= wireNum)) ? HIGH : LOW;
    digitalWrite(channelOrder[i], value);
    currentPattern[i] = (value == HIGH) ? 1 : 0;
  }
}

void ELSequencer::lightWiresByPattern(uint8_t pattern[]) {
  for (uint8_t i = 0; i < channelCount; i++) {
    uint8_t value = pattern[i] > 0 ? HIGH : LOW;
    digitalWrite(channelOrder[i], value);
    currentPattern[i] = (value == HIGH) ? 1 : 0;
  }
}

void ELSequencer::lightAll() {
  for (uint8_t i = 0; i < channelCount; i++) {
    digitalWrite(channelOrder[i], HIGH);
    currentPattern[i] = 1;
  }
}

void ELSequencer::lightNone() {
  for (uint8_t i = 0; i < channelCount; i++) {
    digitalWrite(channelOrder[i], LOW);
    currentPattern[i] = 0;
  }
}

void ELSequencer::lightRandomWires() {
  for (uint8_t i = 0; i < channelCount; i++) {
    uint8_t value = random(0, 2) > 0 ? HIGH : LOW;
    digitalWrite(channelOrder[i], value);
    currentPattern[i] = (value == HIGH) ? 1 : 0;
  }
}

void ELSequencer::lightNumRandomWires(uint8_t numWires) {
  if (numWires > channelCount) {
    numWires = channelCount;
  }
  for (uint8_t i = 0; i < channelCount; i++) {
    uint8_t j = random(0, channelCount);
    uint8_t temp = channelIndices[i];
    channelIndices[i] = channelIndices[j];
    channelIndices[j] = temp;
  }
  for (uint8_t i = 0; i < channelCount; i++) {
    digitalWrite(channelOrder[i], LOW);
    currentPattern[i] = 0;
  }
  for (uint8_t i = 0; i < numWires; i++) {
    digitalWrite(channelOrder[channelIndices[i]], HIGH);
    currentPattern[channelIndices[i]] = 1;
  }
}

void ELSequencer::initSequencer() {
  for (uint8_t i = 0; i < channelCount; i++) {
    pinMode(channelOrder[i], OUTPUT);
    digitalWrite(channelOrder[i], LOW);
    currentPattern[i] = 0;
  }
}

void ELSequencer::playWireStartSequence() {
  for (int i = 0; i <= channelCount; i++) {
    lightNumWires(i);
    delay(100);
  }
  for (int i = channelCount; i >= 0; i--) {
    lightNumWires(i);
    delay(100);
  }
  for (int i = 0; i < 10; i++) {
    lightNumWires(0);
    delay(50);
    lightNumWires(channelCount);
    delay(50);
  }
}

void ELSequencer::getCurrentPattern(uint8_t* out) const {
  if (!out) return;
  for (uint8_t i = 0; i < channelCount; i++) {
    out[i] = currentPattern[i];
  }
}

bool ELSequencer::isChannelOn(uint8_t idx) const {
  if (idx >= channelCount) return false;
  return currentPattern[idx] != 0;
}