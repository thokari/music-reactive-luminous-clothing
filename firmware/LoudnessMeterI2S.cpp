#include "LoudnessMeterI2S.h"
#include <driver/i2s.h>
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
  rmsLow(defaultRmsLow), rmsHigh(defaultRmsHigh) {}

void LoudnessMeterI2S::begin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = sampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;   // change to ONLY_RIGHT if L R is tied high
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = bckPin;
  pins.ws_io_num = wsPin;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = sdPin;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_set_clk(I2S_NUM_0, sampleRate, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void LoudnessMeterI2S::readAudioSample() {
  if (mode == PEAK_TO_PEAK) samplePeakToPeak(); else sampleRms();
}

void LoudnessMeterI2S::samplePeakToPeak() {
  const uint32_t end = micros() + windowMicros;
  int16_t minV = INT16_MAX;
  int16_t maxV = INT16_MIN;
  int32_t buf[128];
  size_t bytes = 0;

  while ((int32_t)(micros() - end) < 0) {
    if (i2s_read(I2S_NUM_0, buf, sizeof(buf), &bytes, portMAX_DELAY) == ESP_OK && bytes) {
      size_t n = bytes / sizeof(int32_t);
      for (size_t i = 0; i < n; ++i) {
        int32_t s32 = buf[i] >> 8;         // 24 bit signed in 32
        int16_t s16 = (int16_t)s32;        // down to 16 bit
        if (s16 < minV) minV = s16;
        if (s16 > maxV) maxV = s16;
      }
    }
  }
  uint32_t p2p = (maxV > minV) ? (uint32_t)(maxV - minV) : 0;
  signal = p2p > 0xFFFF ? 0xFFFF : (uint16_t)p2p;
}

void LoudnessMeterI2S::sampleRms() {
  const uint32_t end = micros() + windowMicros;
  uint64_t sum = 0;
  uint32_t count = 0;

  int32_t buf[128];
  size_t bytes = 0;

  while ((int32_t)(micros() - end) < 0) {
    if (i2s_read(I2S_NUM_0, (void*)buf, sizeof(buf), &bytes, 0) == ESP_OK && bytes) {
      size_t n = bytes / sizeof(int32_t);
      for (size_t i = 0; i < n; ++i) {
        int32_t s = (buf[i] >> 8); // scale to 24->16
        int32_t v = s;
        sum += (int64_t)v * (int64_t)v;
      }
      count += n;
    }
  }
  if (count == 0) { signal = 0; return; }
  float mean = (float)sum / (float)count;
  uint32_t rms = (uint32_t)sqrtf(mean);
  signal = (rms > 0xFFFF) ? 0xFFFF : (uint16_t)rms;
}

void LoudnessMeterI2S::setLow(uint16_t low) {
  if (mode == PEAK_TO_PEAK) peakToPeakLow = low; else rmsLow = low;
}

void LoudnessMeterI2S::setHigh(uint16_t high) {
  if (mode == PEAK_TO_PEAK) peakToPeakHigh = high; else rmsHigh = high;
}

void LoudnessMeterI2S::setMode(Mode m) { mode = m; }
