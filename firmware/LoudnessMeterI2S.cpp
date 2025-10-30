#include "LoudnessMeterI2S.h"
#include "driver/i2s.h"
#include "math.h"

#define USE_INMP441  // Uncomment for INMP441, comment out for SPH0645

#ifdef USE_INMP441
  #define BIT_SHIFT 12
  #define CHANNEL_FMT I2S_CHANNEL_FMT_ONLY_LEFT
  #define COMM_FMT I2S_COMM_FORMAT_I2S
#else
  #include "soc/i2s_reg.h"
  #define BIT_SHIFT 14
  #define CHANNEL_FMT I2S_CHANNEL_FMT_RIGHT_LEFT
  #define COMM_FMT (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB)
  #define FILTER_ZERO_VALUES  // SPH0645 needs zero filtering
#endif

LoudnessMeterI2S::LoudnessMeterI2S(uint8_t bckPin, uint8_t wsPin, uint8_t sdPin,
                                   uint8_t sampleWindowMs,
                                   uint16_t defaultPeakToPeakLow, uint16_t defaultPeakToPeakHigh,
                                   uint16_t defaultRmsLow, uint16_t defaultRmsHigh,
                                   uint32_t sampleRate)
: bckPin(bckPin), wsPin(wsPin), sdPin(sdPin),
  sampleRate(sampleRate),
  windowMicros((uint32_t)sampleWindowMs * 1000UL),
  peakToPeakLow(defaultPeakToPeakLow), peakToPeakHigh(defaultPeakToPeakHigh),
  rmsLow(defaultRmsLow), rmsHigh(defaultRmsHigh),
  gain(HIGH_GAIN),
  mode(PEAK_TO_PEAK),
  signal(0) {}

void LoudnessMeterI2S::begin() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = CHANNEL_FMT,
    .communication_format = COMM_FMT,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  i2s_pin_config_t pins = {
    .bck_io_num = bckPin,
    .ws_io_num = wsPin,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = sdPin
  };
  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
#ifndef USE_INMP441
  REG_SET_BIT(I2S_TIMING_REG(I2S_NUM_0), BIT(9));
  REG_SET_BIT(I2S_CONF_REG(I2S_NUM_0), I2S_RX_MSB_SHIFT);
#endif
  i2s_set_pin(I2S_NUM_0, &pins);
}

void LoudnessMeterI2S::readAudioSample() {
  if (mode == PEAK_TO_PEAK) {
    samplePeakToPeak();
  } else {
    sampleRms();
  }
}

void LoudnessMeterI2S::samplePeakToPeak() {
  const uint32_t startTime = micros();
  int32_t audioBuf[512]; // make sure this can hold all samples!
  
  int32_t globalMin = INT32_MAX;
  int32_t globalMax = INT32_MIN;
  
  while (micros() - startTime < windowMicros) {
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, audioBuf, sizeof(audioBuf), &bytesRead, 0);
    
    if (bytesRead == 0) continue;
    
    size_t samplesRead = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samplesRead; i++) {
#ifdef FILTER_ZERO_VALUES
      if (audioBuf[i] == 0 || audioBuf[i] == -1) continue;
#endif
      int32_t sample = audioBuf[i] >> BIT_SHIFT;
      if (sample < globalMin) globalMin = sample;
      if (sample > globalMax) globalMax = sample;
    }
  }
  int32_t rawVolume = globalMax - globalMin;
  float scaleFactor = getScaleFactor();
  signal = (uint16_t)(rawVolume / scaleFactor);
}

void LoudnessMeterI2S::sampleRms() {
  const uint32_t startTime = micros();
  int32_t audioBuf[256];
  
  uint64_t sumSquares = 0;
  uint32_t sampleCount = 0;
  
  while (micros() - startTime < windowMicros) {
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, audioBuf, sizeof(audioBuf), &bytesRead, 0);
    
    if (bytesRead == 0) continue;
    
    size_t samplesRead = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samplesRead; i++) {
#ifdef FILTER_ZERO_VALUES
      if (audioBuf[i] == 0 || audioBuf[i] == -1) continue;
#endif
      int32_t sample = audioBuf[i] >> BIT_SHIFT;
      sumSquares += (int64_t)sample * (int64_t)sample;
      sampleCount++;
    }
  }
  if (sampleCount == 0) {
    signal = 0;
    return;
  }
  float meanSquare = (float)sumSquares / (float)sampleCount;
  int32_t rawRms = (int32_t)sqrtf(meanSquare);
  float scaleFactor = getScaleFactor();
  signal = (uint16_t)(rawRms / scaleFactor);
}

float LoudnessMeterI2S::getScaleFactor() const {
  switch(gain) {
    case LOW_GAIN:
      return 300.0;
    case MEDIUM_GAIN:
      return 60.0;
    case HIGH_GAIN:
      return 15.00;
    default:
      return 15.00;
  }
}

void LoudnessMeterI2S::setLow(uint16_t low) {
  if (mode == PEAK_TO_PEAK) {
    peakToPeakLow = low;
  } else {
    rmsLow = low;
  }
}

void LoudnessMeterI2S::setHigh(uint16_t high) {
  if (mode == PEAK_TO_PEAK) {
    peakToPeakHigh = high;
  } else {
    rmsHigh = high;
  }
}

void LoudnessMeterI2S::setMode(Mode m) {
  mode = m;
}
