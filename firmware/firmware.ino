#define DEBUG 1
#define DEBUG_BAUD_RATE 115200
#define USE_PUSH_BUTTONS 0
#define USE_I2S_MIC 1

// LoudnessMeter (ADC vs I2S)
#define MIC_SAMPLE_WINDOW 14 // ms
#define DEFAULT_P2P_LOW 1000
#define DEFAULT_P2P_HIGH 4000
#define DEFAULT_RMS_LOW 1000
#define DEFAULT_RMS_HIGH 1400
#define MAX_MAPPED_VALUE 8

//#if USE_I2S_MIC
#include "LoudnessMeterI2S.h"
#define BCK_BCLK 26
#define WS_LRCL 25
#define SD_DOUT 22
 LoudnessMeterI2S mic(
  BCK_BCLK, WS_LRCL, SD_DOUT,
  MIC_SAMPLE_WINDOW,
  DEFAULT_P2P_LOW, DEFAULT_P2P_HIGH,
  DEFAULT_RMS_LOW, DEFAULT_RMS_HIGH,
  22050
);
//#else
//#include "LoudnessMeter.h"
//#define MIC_OUT 35
//#define MIC_GAIN 32
//LoudnessMeter mic = LoudnessMeter(
//  MIC_OUT, MIC_GAIN, MIC_SAMPLE_WINDOW,
//  DEFAULT_P2P_LOW, DEFAULT_P2P_HIGH,
//  DEFAULT_RMS_LOW, DEFAULT_RMS_HIGH);
//#endif
uint16_t mappedSignal;

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
#define CHANNEL_C 2
#define CHANNEL_D 0
#define CHANNEL_E 4
#define CHANNEL_F 16
#define CHANNEL_G 17
#define CHANNEL_H 5
#define ACTIVE_CHANNELS 8
const uint8_t channelOrder[ACTIVE_CHANNELS] = {
  CHANNEL_A, CHANNEL_B, CHANNEL_C, CHANNEL_D, CHANNEL_E, CHANNEL_F, CHANNEL_G, CHANNEL_H
};
ELSequencer sequencer = ELSequencer(channelOrder, ACTIVE_CHANNELS);
uint8_t mode = 0;
uint8_t numWires = 8;
#define NUM_DELAYS 10
uint16_t fixedModeDelays[NUM_DELAYS] = { 10, 25, 33, 50, 66, 100, 166, 250, 500, 1000 };
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
#if USE_RADIO
  initRadio();
#endif
#if USE_PUSH_BUTTONS
  pushButtonsBegin(BUTTON_1_PIN, DEBOUNCE_MS, BUTTON_PAUSE_MS);
#endif
  sequencer.begin();
  pinMode(ADDITIONAL_GND_PIN, OUTPUT);
  digitalWrite(ADDITIONAL_GND_PIN, LOW);
#if DEBUG
  Serial.println("Setup complete");
#endif
}

boolean outputToBluetooth = false;
uint32_t loopBegin = 0;

void loop() {
  loopBegin = millis();
#if USE_PUSH_BUTTONS
  pushButtonsUpdate(loopBegin);
  if (pushButtonsShouldSkipLoop()) {
    if (pushButtonConsumePressed()) {
      fixedFlashWithDecay();
    }
    return;
  }
#endif
  bluetooth.handleInput();
  if (isReactive(mode)) {
    mic.readAudioSample();
    processSample();
    modes[mode].run();
    if (outputToBluetooth) {
      printToBluetooth();
    }
#if DEBUG
    printToSerialMonitor();
#endif
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

void fixedPulseUp() {
  for (int i = 0; i <= ACTIVE_CHANNELS; i++) {
    sequencer.lightWiresAtIndex(i);
    delay(currentDelay());
  }
}

void fixedPulseUpDown() {
  for (int i = 0; i <= ACTIVE_CHANNELS - 1; i++) {
    sequencer.lightWiresAtIndex(i);
    delay(currentDelay());
  }
  for (int i = ACTIVE_CHANNELS - 2; i >= 1; i--) {
    sequencer.lightWiresAtIndex(i);
    delay(currentDelay());
  }
}

void fixedFlash() {
  sequencer.lightAll();
  delay(currentDelay());
  sequencer.lightNone();
  delay(currentDelay());
}

void fixedFlashWithDecay() {
  sequencer.lightAll();
  delay(currentDelay());
  for (int i = ACTIVE_CHANNELS - 1; i >= 0; i--) {
    sequencer.lightNumWires(i);
    delay(currentDelay());
  }
}

void fixedRandom() {
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
  #if USE_I2S_MIC
    mic.setMode(LoudnessMeterI2S::PEAK_TO_PEAK);
  #else
    mic.setMode(LoudnessMeter::PEAK_TO_PEAK);
  #endif
  bluetooth.sendKwlString("P2P", "P");
}

void cmdSetSamplingRMS(const String&) {
  #if USE_I2S_MIC
    mic.setMode(LoudnessMeterI2S::RMS);
  #else
    mic.setMode(LoudnessMeter::RMS);
  #endif
  bluetooth.sendKwlString("RMS", "P");
}

void cmdSetGain(const String& parameter) {
  int gain = parameter.toInt();
  #if !USE_I2S_MIC
    if (gain == 1) {
      mic.setGain(LoudnessMeter::LOW_GAIN);
    } else if (gain == 2) {
      mic.setGain(LoudnessMeter::MEDIUM_GAIN);
    } else if (gain == 3) {
      mic.setGain(LoudnessMeter::HIGH_GAIN);
    }
  #endif
  bluetooth.sendKwlValue(gain, "N");
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

void printToBluetooth() {
  String data = String(mic.getSignal()) + "," + String(mic.getLow()) + "," + String(mic.getHigh());
  bluetooth.sendKwlString(data, "G");
}

// ---------------- PROCESSING ----------------
void processSample() {
#if DEBUG
  Serial.print(mic.getSignal());
  Serial.print(",");
#endif
  uint16_t constrainedSignal = constrain(mic.getSignal(), mic.getLow(), mic.getHigh());
  mappedSignal = map(constrainedSignal, mic.getLow(), mic.getHigh(), 0, ACTIVE_CHANNELS);
}

uint16_t currentDelay() {
  return fixedModeDelays[currentDelayIndex];
}

// ---------------- DEBUGGING ----------------
void printToSerialMonitor() {
  Serial.print(mic.getLow());
  Serial.print(",");
  Serial.print(mic.getHigh());
  Serial.print(",");
  Serial.print(millis() - loopBegin);
  Serial.println();
}
