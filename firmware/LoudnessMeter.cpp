#define DEBUG 0
#define DEBUG_BAUD_RATE 57600

#include "LoudnessMeter.h"

LoudnessMeter::LoudnessMeter(
  uint8_t micOut, uint8_t micGain, uint8_t micSampleWindow,
  uint16_t defaultPeakToPeakLow, uint16_t defaultPeakToPeakHigh,
  uint16_t defaultRmsLow, uint16_t defaultRmsHigh) {

  this->micOut = micOut;
  this->micGain = micGain;
  this->micSampleWindow = micSampleWindow;
  this->peakToPeakLow = defaultPeakToPeakLow;
  this->peakToPeakHigh = defaultPeakToPeakHigh;
  this->rmsLow = defaultRmsLow;
  this->rmsHigh = defaultRmsHigh;
  this->mode = PEAK_TO_PEAK;
  this->gain = HIGH_GAIN;
}

void LoudnessMeter::begin() {
  pinMode(micOut, INPUT);
  setGain(gain);
#if DEBUG
  Serial.begin(DEBUG_BAUD_RATE);
#endif
}

void LoudnessMeter::readAudioSample() {
  switch (mode) {
    case PEAK_TO_PEAK:
      samplePeakToPeak();
      break;
    case RMS:
      sampleRms();
      break;
  }
}

void LoudnessMeter::samplePeakToPeak() {
  uint16_t currentMin = MAX_SIGNAL;
  uint16_t currentMax = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < micSampleWindow) {
    uint16_t currentSample = analogRead(micOut);
    currentMin = min(currentMin, currentSample);
    currentMax = max(currentMax, currentSample);
  }
  signal = currentMax - currentMin;
}

void LoudnessMeter::sampleRms() {
  uint16_t currentSample = 0;
  uint32_t currentSum = 0;
  uint32_t t0 = millis();
  uint32_t sampleCount = 0;
  while (millis() - t0 < micSampleWindow) {
    currentSample = analogRead(micOut);
    currentSum += pow(currentSample, 2);
    sampleCount++;
  }
  signal = sqrt(currentSum / sampleCount);
}

void LoudnessMeter::setLow(uint16_t low) {
  switch (mode) {
    case PEAK_TO_PEAK:
      peakToPeakLow = low;
      break;
    case RMS:
      rmsLow = low;
      break;
  }
}

void LoudnessMeter::setHigh(uint16_t high) {
  switch (mode) {
    case PEAK_TO_PEAK:
      peakToPeakHigh = high;
      break;
    case RMS:
      rmsHigh = high;
      break;
  }
}

void LoudnessMeter::setGain(Gain gain) {
  switch (gain) {
    case HIGH_GAIN:
      pinMode(micGain, INPUT);
      break;
    case MEDIUM_GAIN:
      pinMode(micGain, OUTPUT);
      digitalWrite(micGain, LOW);
      break;
    case LOW_GAIN:
      pinMode(micGain, OUTPUT);
      digitalWrite(micGain, HIGH);
      break;
  }
}

uint16_t LoudnessMeter::getSignal() {
  return signal;
}

uint16_t LoudnessMeter::getLow() {
  switch (mode) {
    case PEAK_TO_PEAK:
      return peakToPeakLow;
    case RMS:
      return rmsLow;
  }
}

uint16_t LoudnessMeter::getHigh() {
  switch (mode) {
    case PEAK_TO_PEAK:
      return peakToPeakHigh;
    case RMS:
      return rmsHigh;
  }
}

void LoudnessMeter::setMode(Mode mode) {
  this->mode = mode;
}
