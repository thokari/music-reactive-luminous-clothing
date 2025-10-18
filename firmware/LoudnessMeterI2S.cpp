#include "LoudnessMeterI2S.h"
#include <driver/i2s.h>
#include "soc/i2s_reg.h"
#include <math.h>

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
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
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
  REG_SET_BIT(I2S_TIMING_REG(I2S_NUM_0), BIT(9));
  REG_SET_BIT(I2S_CONF_REG(I2S_NUM_0), I2S_RX_MSB_SHIFT);
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
  int32_t audioBuf[256];
  int32_t cleanBuf[128];
  
  int32_t globalMin = 32767;
  int32_t globalMax = -32768;
  
  while (micros() - startTime < windowMicros) {
    size_t bytesRead = 0;
    esp_err_t result = i2s_read(I2S_NUM_0, audioBuf, sizeof(audioBuf), &bytesRead, portMAX_DELAY);
    
    if (result != ESP_OK || bytesRead == 0) {
      continue;
    }
    
    // Filter and shift samples
    int cleanIdx = 0;
    for (int i = 0; i < 256 && cleanIdx < 128; i++) {
      if (audioBuf[i] != 0 && audioBuf[i] != -1) {
        cleanBuf[cleanIdx] = audioBuf[i] >> 14;
        cleanIdx++;
      }
    }
    
    if (cleanIdx < 10) {
      continue;
    }
    
    // Calculate mean
    long long sum = 0;
    for (int i = 0; i < cleanIdx; i++) {
      sum += cleanBuf[i];
    }
    int32_t meanval = sum / cleanIdx;
    
    // Find min/max
    for (int i = 0; i < cleanIdx; i++) {
      int32_t sample = cleanBuf[i] - meanval;
      if (sample < globalMin) globalMin = sample;
      if (sample > globalMax) globalMax = sample;
    }
  }
  
  // Calculate peak-to-peak and scale to 0-4096 range
  int32_t rawVolume = globalMax - globalMin;
  float scaleFactor = getScaleFactor();
  uint16_t scaled = (uint16_t)(rawVolume / scaleFactor);
  signal = (scaled > 4096) ? 4096 : scaled;
}

void LoudnessMeterI2S::sampleRms() {
  const uint32_t startTime = micros();
  int32_t audioBuf[256];
  int32_t cleanBuf[128];
  
  uint64_t sumSquares = 0;
  uint32_t totalSamples = 0;
  
  while (micros() - startTime < windowMicros) {
    size_t bytesRead = 0;
    esp_err_t result = i2s_read(I2S_NUM_0, audioBuf, sizeof(audioBuf), &bytesRead, portMAX_DELAY);
    
    if (result != ESP_OK || bytesRead == 0) {
      continue;
    }
    
    // Filter and shift samples
    int cleanIdx = 0;
    for (int i = 0; i < 256 && cleanIdx < 128; i++) {
      if (audioBuf[i] != 0 && audioBuf[i] != -1) {
        cleanBuf[cleanIdx] = audioBuf[i] >> 14;
        cleanIdx++;
      }
    }
    
    if (cleanIdx < 10) {
      continue;
    }
    
    // Calculate mean for DC offset removal
    long long sum = 0;
    for (int i = 0; i < cleanIdx; i++) {
      sum += cleanBuf[i];
    }
    int32_t meanval = sum / cleanIdx;
    
    // Calculate sum of squares
    for (int i = 0; i < cleanIdx; i++) {
      int32_t sample = cleanBuf[i] - meanval;
      sumSquares += (int64_t)sample * (int64_t)sample;
      totalSamples++;
    }
  }
  
  if (totalSamples == 0) {
    signal = 0;
    return;
  }
  
  // Calculate RMS and scale to 0-4096 range
  float meanSquare = (float)sumSquares / (float)totalSamples;
  int32_t rawRms = (int32_t)sqrtf(meanSquare);
  float scaleFactor = getScaleFactor();
  uint16_t scaled = (uint16_t)(rawRms / scaleFactor);
  signal = (scaled > 4096) ? 4096 : scaled;
}

float LoudnessMeterI2S::getScaleFactor() const {
  switch(gain) {
    case LOW_GAIN:    // Gain 1 - festivals (least sensitive)
      return 75.0;    // 300k / 75 = 4000
    case MEDIUM_GAIN: // Gain 2 - clubs
      return 37.5;    // 150k / 37.5 = 4000
    case HIGH_GAIN:   // Gain 3 - normal/quiet (most sensitive)
      return 18.75;   // 75k / 18.75 = 4000
    default:
      return 37.5;
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
