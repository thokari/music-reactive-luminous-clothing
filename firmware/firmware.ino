#define DEBUG 0
#define DEBUG_BAUD_RATE 57600
#define USE_PUSH_BUTTONS 0
#define GRAPH_MIN_INTERVAL_MS 20

// LoudnessMeter
#include "LoudnessMeter.h"
#define MIC_OUT 35
#define MIC_GAIN 32
#define MIC_SAMPLE_WINDOW 14 // ms
#define DEFAULT_P2P_LOW 1000
#define DEFAULT_P2P_HIGH 4000
#define DEFAULT_RMS_LOW 1000
#define DEFAULT_RMS_HIGH 1400
#define MAX_MAPPED_VALUE 8
LoudnessMeter mic = LoudnessMeter(
  MIC_OUT, MIC_GAIN, MIC_SAMPLE_WINDOW,
  DEFAULT_P2P_LOW, DEFAULT_P2P_HIGH,
  DEFAULT_RMS_LOW, DEFAULT_RMS_HIGH);
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
  if (mappedSignal > 6) {
    sequencer.lightNumRandomWires(numWires);
  }
}

void reactiveRandomSwap() {
  const uint8_t N = ACTIVE_CHANNELS;
  uint8_t pattern[N];
  sequencer.getCurrentPattern(pattern);

  // Build ON/OFF lists
  uint8_t onIdx[N];
  uint8_t offIdx[N];
  uint8_t onCount = 0, offCount = 0;
  for (uint8_t i = 0; i < N; i++) {
    if (pattern[i]) onIdx[onCount++] = i; else offIdx[offCount++] = i;
  }

  // Maintain exactly numWires lit
  uint8_t target = numWires > N ? N : numWires;
  if (onCount < target) {
    uint8_t need = target - onCount;
    for (uint8_t k = 0; k < need && offCount > 0; ++k) {
      uint8_t pick = random(offCount);
      uint8_t idx = offIdx[pick];
      pattern[idx] = 1;
      offIdx[pick] = offIdx[offCount - 1];
      offCount--;
      onIdx[onCount++] = idx;
    }
  } else if (onCount > target) {
    uint8_t need = onCount - target;
    for (uint8_t k = 0; k < need && onCount > 0; ++k) {
      uint8_t pick = random(onCount);
      uint8_t idx = onIdx[pick];
      pattern[idx] = 0;
      onIdx[pick] = onIdx[onCount - 1];
      onCount--;
      offIdx[offCount++] = idx;
    }
  }

  if (mappedSignal > 6) {
    // Replace up to 'target' currently-on wires with new ones from OFF
    if (target > 0 && offCount > 0 && onCount > 0) {
      uint8_t k = offCount < target ? offCount : target;
      for (uint8_t t = 0; t < k; ++t) {
        uint8_t pickOff = random(offCount);
        uint8_t addIdx = offIdx[pickOff];
        offIdx[pickOff] = offIdx[offCount - 1];
        offCount--;

        uint8_t pickOn = random(onCount);
        uint8_t remIdx = onIdx[pickOn];
        pattern[remIdx] = 0;
        onIdx[pickOn] = onIdx[onCount - 1];
        onCount--;
        offIdx[offCount++] = remIdx;

        pattern[addIdx] = 1;
        onIdx[onCount++] = addIdx;
      }
    }
  } else if (mappedSignal > 4) {
    // Low: swap exactly one if possible
    if (onCount > 0 && offCount > 0) {
      uint8_t pickOn = random(onCount);
      uint8_t pickOff = random(offCount);
      uint8_t idxOn = onIdx[pickOn];
      uint8_t idxOff = offIdx[pickOff];
      pattern[idxOn] = 0;
      pattern[idxOff] = 1;
    }
  }
  sequencer.lightWiresByPattern(pattern);
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
  mic.setMode(LoudnessMeter::PEAK_TO_PEAK);
  bluetooth.sendKwlString("P2P", "P");
}

void cmdSetSamplingRMS(const String&) {
  mic.setMode(LoudnessMeter::RMS);
  bluetooth.sendKwlString("RMS", "P");
}

void cmdSetGain(const String& parameter) {
  int gain = parameter.toInt();
  if (gain == 1) {
    mic.setGain(LoudnessMeter::LOW_GAIN);
  } else if (gain == 2) {
    mic.setGain(LoudnessMeter::MEDIUM_GAIN);
  } else if (gain == 3) {
    mic.setGain(LoudnessMeter::HIGH_GAIN);
  }
  bluetooth.sendKwlValue(gain, "N");
}

void cmdUp(const String&) {
  nextMode();
}

void cmdDown(const String&) {
  prevMode();
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
  static uint32_t lastGraphMs = 0;
  uint32_t now = millis();
  if (now - lastGraphMs < GRAPH_MIN_INTERVAL_MS) return;
  lastGraphMs = now;
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
