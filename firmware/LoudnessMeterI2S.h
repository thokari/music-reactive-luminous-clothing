#pragma once
#include <Arduino.h>
class LoudnessMeterI2S {
public:
  enum Mode { PEAK_TO_PEAK, RMS };
  
  LoudnessMeterI2S(uint8_t bckPin, uint8_t wsPin, uint8_t sdPin,
                   uint8_t sampleWindowMs,
                   uint16_t defaultPeakToPeakLow, uint16_t defaultPeakToPeakHigh,
                   uint16_t defaultRmsLow, uint16_t defaultRmsHigh,
                   uint32_t sampleRate = 16000);
  
  void begin();
  void readAudioSample();
  void setLow(uint16_t low);
  void setHigh(uint16_t high);
  void setMode(Mode mode);
  void setGain(uint16_t g) { scaleFactor = g; }
  
  uint16_t getSignal() const { return signal; }
  uint16_t getLow()    const { return (mode == PEAK_TO_PEAK) ? peakToPeakLow : rmsLow; }
  uint16_t getHigh()   const { return (mode == PEAK_TO_PEAK) ? peakToPeakHigh : rmsHigh; }
  uint16_t getGain()   const { return scaleFactor; }
  
private:
  void samplePeakToPeak();
  void sampleRms();
  
  uint8_t bckPin, wsPin, sdPin;
  uint32_t sampleRate;
  uint32_t windowMicros;
  uint32_t signal = 0;
  uint16_t peakToPeakLow, peakToPeakHigh, rmsLow, rmsHigh;
  Mode mode = PEAK_TO_PEAK;
  uint16_t scaleFactor = 15;
};