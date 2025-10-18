#pragma once
#include <Arduino.h>
class LoudnessMeterI2S {
public:
  enum Mode { PEAK_TO_PEAK, RMS };
  enum Gain { HIGH_GAIN, MEDIUM_GAIN, LOW_GAIN };
  
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
  void setGain(Gain g) { gain = g; }  // Actually store the gain!
  
  uint16_t getSignal() const { return signal; }
  uint16_t getLow()    const { return (mode == PEAK_TO_PEAK) ? peakToPeakLow : rmsLow; }
  uint16_t getHigh()   const { return (mode == PEAK_TO_PEAK) ? peakToPeakHigh : rmsHigh; }
  
private:
  void samplePeakToPeak();
  void sampleRms();
  float getScaleFactor() const;  // Helper to get scale factor based on gain
  
  uint8_t bckPin, wsPin, sdPin;
  uint32_t sampleRate;
  uint32_t windowMicros;
  volatile uint16_t signal = 0;
  uint16_t peakToPeakLow, peakToPeakHigh, rmsLow, rmsHigh;
  Mode mode = PEAK_TO_PEAK;
  Gain gain = HIGH_GAIN;  // Add gain field!
};
