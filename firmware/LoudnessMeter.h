#ifndef LOUDNESSMETER_H
#define LOUDNESSMETER_H

#define MAX_SIGNAL 4095

#include "Arduino.h"

class LoudnessMeter {
public:
  enum Mode {
    RMS,
    PEAK_TO_PEAK
  };
  enum Gain {
    HIGH_GAIN,
    MEDIUM_GAIN,
    LOW_GAIN
  };

  LoudnessMeter(
    uint8_t micOut, uint8_t micGain, uint8_t micSampleWindow,
    uint16_t defaultPeakToPeakLow, uint16_t defaultPeakToPeakHigh,
    uint16_t defaultRmsLow, uint16_t defaultRmsHigh);

  void begin();
  void readAudioSample();
  void setLow(uint16_t low);
  void setHigh(uint16_t high);
  void setGain(Gain gain);
  void setMode(Mode mode);
  uint16_t getSignal();
  uint16_t getLow();
  uint16_t getHigh();

private:
  void samplePeakToPeak();
  void sampleRms();
  uint8_t micOut;
  uint8_t micGain;
  uint8_t micSampleWindow;
  uint16_t signal;
  uint16_t peakToPeakLow;
  uint16_t peakToPeakHigh;
  uint16_t rmsLow;
  uint16_t rmsHigh;
  Mode mode;
  Gain gain;
};

#endif
