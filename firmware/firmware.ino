#define DEBUG 0
#define DEBUG_BAUD_RATE 115200
#define USE_PUSH_BUTTONS 0

// LoudnessMeter (I2S)
#define MIC_SAMPLE_WINDOW 14 // ms
#define DEFAULT_P2P_LOW 1000
#define DEFAULT_P2P_HIGH 4000
#define DEFAULT_RMS_LOW 1000
#define DEFAULT_RMS_HIGH 1400
#define MAX_MAPPED_VALUE 8

#include "LoudnessMeterI2S.h"
#define BCK_BCLK 26
#define WS_LRCL 25
#define SD_DOUT 27
LoudnessMeterI2S mic(
  BCK_BCLK, WS_LRCL, SD_DOUT,
  MIC_SAMPLE_WINDOW,
  DEFAULT_P2P_LOW, DEFAULT_P2P_HIGH,
  DEFAULT_RMS_LOW, DEFAULT_RMS_HIGH,
  22050
);
uint16_t mappedSignal;

// Parallel processing
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
QueueHandle_t audioQueue = NULL;
TaskHandle_t audioTaskHandle = NULL;

// Bluetooth
#include "BluetoothElectronics.h"
#define DEVICE_NAME "LOLIN32 Lite - TEST"
BluetoothElectronics bluetooth = BluetoothElectronics(DEVICE_NAME);

// EL Sequencer
#include "ELSequencer.h"
#include "Modes.h"
#include "ModeRegistry.h"
#define CHANNEL_A 13
#define CHANNEL_B 15
#define CHANNEL_C 0
#define CHANNEL_D 2
#define CHANNEL_E 16
#define CHANNEL_F 4
#define CHANNEL_G 17
#define CHANNEL_H 5
#define ACTIVE_CHANNELS 8
const uint8_t channelOrder[ACTIVE_CHANNELS] = {
  CHANNEL_C, CHANNEL_D, CHANNEL_B, CHANNEL_A, CHANNEL_H, CHANNEL_G, CHANNEL_E, CHANNEL_F
};
ELSequencer sequencer = ELSequencer(channelOrder, ACTIVE_CHANNELS);
uint8_t mode = 0;
uint8_t numWires = 8;
#define NUM_DELAYS 10
uint16_t periodicModeDelays[NUM_DELAYS] = { 10, 25, 33, 50, 66, 100, 166, 250, 500, 1000 };
uint8_t currentDelayIndex = 1;
uint32_t timer = 0;
#define ADDITIONAL_GND_PIN 18

// Push-Buttons
#if USE_PUSH_BUTTONS
#include "PushButtons.h"
#define BUTTON_1_PIN 25
#define DEBOUNCE_MS 5
#define BUTTON_PAUSE_MS 1000
#endif

void setup() {
#if DEBUG
  Serial.begin(DEBUG_BAUD_RATE);
#endif
  registerBluetoothCommands();
  bluetooth.begin();
  mic.begin();
#if USE_PUSH_BUTTONS
  pushButtonsBegin(BUTTON_1_PIN, DEBOUNCE_MS, BUTTON_PAUSE_MS);
#endif
  sequencer.begin();
  pinMode(ADDITIONAL_GND_PIN, OUTPUT);
  digitalWrite(ADDITIONAL_GND_PIN, LOW);

  audioQueue = xQueueCreate(1, sizeof(uint16_t));
  xTaskCreatePinnedToCore(
    audioSamplingTask,
    "AudioSampling",
    4096,             // Stack size (bytes)
    NULL,             // Parameters
    1,                // Priority (1 = low, higher than idle)
    &audioTaskHandle,
    1                 // Core
  );
#if DEBUG
  Serial.println("Setup complete");
#endif
}

boolean outputToBluetooth = false;
uint32_t loopBegin = 0;

void loop() {
  loopBegin = micros();
#if USE_PUSH_BUTTONS
  pushButtonsUpdate(loopBegin);
  if (pushButtonsShouldSkipLoop()) {
    if (pushButtonConsumePressed()) {
      periodicFlashWithDecay();
    }
    return;
  }
#endif
  bluetooth.handleInput();
  if (isReactive(mode)) {
    uint16_t signal;
    if (xQueueReceive(audioQueue, &signal, portMAX_DELAY) == pdTRUE) {
#if DEBUG
      Serial.print(signal);
      Serial.print(",");
#endif
      uint16_t constrainedSignal = constrain(signal, mic.getLow(), mic.getHigh());
      mappedSignal = map(constrainedSignal, mic.getLow(), mic.getHigh(), 0, ACTIVE_CHANNELS);
      modes[mode].run();
      if (outputToBluetooth) {
        printToBluetooth(signal);
      }
#if DEBUG
      printToSerialMonitor();
#endif
    }
  } else {
    modes[mode].run();
  }
}

// ---------------- MODE DEFINITIONS ----------------
void reactivePulse() {
  if (numWires == 1) {
    mappedSignal > 0 ? sequencer.lightWiresAtIndex(mappedSignal - 1) : sequencer.lightNumWires(0);
  } else if (numWires == ACTIVE_CHANNELS) {
    sequencer.lightNumWires(mappedSignal);
  } else {
    sequencer.lightNumWiresUpToWire(numWires, mappedSignal);
  }
}

uint16_t displayLevel = 0;
uint32_t lastDecayMs = 0;
void reactivePulseWithDecay() {
  if (mappedSignal > displayLevel) {
    displayLevel = mappedSignal;
    lastDecayMs = millis();
  } else {
    if (displayLevel > 0) {
      uint32_t now = millis();
      uint16_t releaseMs = currentDelay();
      if (releaseMs == 0) {
        releaseMs = MIC_SAMPLE_WINDOW;
      }
      if (now - lastDecayMs >= releaseMs) {
        displayLevel -= 1;
        lastDecayMs = now;
      }
    }
  }
  sequencer.lightNumWiresUpToWire(numWires, displayLevel);
}

void reactiveRandomSimple() {
  static uint16_t last = 0;
  bool rising = mappedSignal > last;
  last = mappedSignal;
  if (rising && mappedSignal > 6) {
    sequencer.lightNumRandomWires(numWires);
  }
}

void reactiveRandomHighLow() {
  static uint16_t last = 0;
  static uint32_t lastHighMs = 0;

  const uint8_t TH_MED = 5;
  const uint8_t TH_HIGH = 7;
  const uint32_t LOW_MODE_COOLDOWN_MS = 1000;

  uint16_t cur = mappedSignal;
  uint32_t now = millis();
  bool rising = cur > last;
  last = cur;

  if (!rising) return;

  if (cur >= TH_HIGH) {
    lastHighMs = now;
    uint8_t k = (numWires > ACTIVE_CHANNELS) ? ACTIVE_CHANNELS : numWires;
    sequencer.lightNumRandomWires(k);
    return;
  }

  if (cur >= TH_MED && (now - lastHighMs) >= LOW_MODE_COOLDOWN_MS) {
    sequencer.lightNumRandomWires(1);
  }
}

void periodicPulseUp() {
  for (int i = 0; i <= ACTIVE_CHANNELS; i++) {
    sequencer.lightWiresAtIndex(i);
    delay(currentDelay());
  }
}

void periodicPulseUpDown() {
  for (int i = 0; i <= ACTIVE_CHANNELS - 1; i++) {
    sequencer.lightWiresAtIndex(i);
    delay(currentDelay());
  }
  for (int i = ACTIVE_CHANNELS - 2; i >= 1; i--) {
    sequencer.lightWiresAtIndex(i);
    delay(currentDelay());
  }
}

void periodicFlash() {
  sequencer.lightAll();
  delay(currentDelay());
  sequencer.lightNone();
  delay(currentDelay());
}

void periodicFlashWithDecay() {
  sequencer.lightAll();
  delay(currentDelay());
  for (int i = ACTIVE_CHANNELS - 1; i >= 0; i--) {
    sequencer.lightNumWires(i);
    delay(currentDelay());
  }
}

void periodicRandom() {
  sequencer.lightRandomWires();
  delay(currentDelay());
}

// ---------------- BLUETOOTH COMMANDS ----------------
void registerBluetoothCommands() {
  bluetooth.registerCommand("L", cmdSetLow);
  bluetooth.registerCommand("H", cmdSetHigh);
  bluetooth.registerCommand("D", cmdDebugOn);
  bluetooth.registerCommand("d", cmdDebugOff);
  bluetooth.registerCommand("S", cmdSetSamplingP2P);
  bluetooth.registerCommand("s", cmdSetSamplingRMS);
  bluetooth.registerCommand("N", cmdSetGain);
  bluetooth.registerCommand("1", cmdUp);
  bluetooth.registerCommand("3", cmdDown);
  bluetooth.registerCommand("2", cmdRight);
  bluetooth.registerCommand("4", cmdLeft);
}

void cmdSetLow(const String& p) {
  uint16_t v = p.toInt();
  if (v >= mic.getHigh()) v = mic.getHigh() - 1;
  mic.setLow(v);
  bluetooth.sendKwlValue(mic.getLow(), "L");
}

void cmdSetHigh(const String& p) {
  uint16_t v = p.toInt();
  if (v <= mic.getLow()) v = mic.getLow() + 1;
  mic.setHigh(v);
  bluetooth.sendKwlValue(mic.getHigh(), "H");
}

void cmdDebugOn(const String&) {
  outputToBluetooth = true;
}

void cmdDebugOff(const String&) {
  outputToBluetooth = false;
}

void cmdSetSamplingP2P(const String&) {
  mic.setMode(LoudnessMeterI2S::PEAK_TO_PEAK);
  bluetooth.sendKwlString("P2P", "P");
}

void cmdSetSamplingRMS(const String&) {
  mic.setMode(LoudnessMeterI2S::RMS);
  bluetooth.sendKwlString("RMS", "P");
}

void cmdSetGain(const String& parameter) {
  uint16_t gain = parameter.toInt();
  
  mic.setGain(gain);
  bluetooth.sendKwlValue((int)gain, "N");
}

void cmdUp(const String&) {
  prevMode();
}

void cmdDown(const String&) {
  nextMode();
}

void cmdRight(const String&) {
  nextSetting();
}

void cmdLeft(const String&) {
  prevSetting();
}

void nextMode() {
  mode = (mode + 1) % getModeCount();
  if (modes[mode].onEnter) modes[mode].onEnter();
  printMode();
}

void prevMode() {
  mode = (mode == 0) ? (getModeCount() - 1) : (mode - 1);
  if (modes[mode].onEnter) modes[mode].onEnter();
  printMode();
}

void nextSetting() {
  if (isReactive(mode)) {
    if (++numWires > ACTIVE_CHANNELS) {
      numWires = 1;
    }
    bluetooth.sendKwlValue(numWires, "S");
  } else {
    currentDelayIndex = (currentDelayIndex + 1) % NUM_DELAYS;
    bluetooth.sendKwlValue(currentDelay(), "S");
  }
}

void prevSetting() {
  if (isReactive(mode)) {
    numWires = numWires - 1;
    if (numWires == 0) {
      numWires = ACTIVE_CHANNELS;
    }
    bluetooth.sendKwlValue(numWires, "S");
  } else {
    if (currentDelayIndex == 0) {
      currentDelayIndex = NUM_DELAYS - 1;
    } else {
      currentDelayIndex--;
    }
    bluetooth.sendKwlValue(currentDelay(), "S");
  }
}

void printMode() {
  bluetooth.sendKwlString(modes[mode].label, "M");
  if (isReactive(mode)) {
    bluetooth.sendKwlValue(numWires, "S");
  } else {
    bluetooth.sendKwlValue(currentDelay(), "S");
  }
}

void printToBluetooth(uint16_t signal) {
  String data = String(signal) + "," + String(mic.getLow()) + "," + String(mic.getHigh());
  bluetooth.sendKwlString(data, "G");
}

// ---------------- PROCESSING ----------------
void audioSamplingTask(void* parameter) {
  while (true) {
    mic.readAudioSample();
    uint16_t signal = mic.getSignal();
    xQueueOverwrite(audioQueue, &signal);
  }
}

uint16_t currentDelay() {
  return periodicModeDelays[currentDelayIndex];
}

// ---------------- DEBUGGING ----------------
void printToSerialMonitor() {
  Serial.print(mic.getLow());
  Serial.print(",");
  Serial.print(mic.getHigh());
  Serial.print(",");
  Serial.print(micros() - loopBegin);
  Serial.println();
}
