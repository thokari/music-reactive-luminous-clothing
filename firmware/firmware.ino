#define DEBUG 0
#define DEBUG_BAUD_RATE 57600
#define USE_PUSH_BUTTONS 0

// LoudnessMeter
#include "LoudnessMeter.h"
#define MIC_OUT 35
#define MIC_GAIN 32
#define MIC_SAMPLE_WINDOW 14 // ms
#define DEFAULT_P2P_LOW 800
#define DEFAULT_P2P_HIGH 1950
#define DEFAULT_RMS_LOW 800
#define DEFAULT_RMS_HIGH 1950
#define MAX_MAPPED_VALUE 8
LoudnessMeter mic = LoudnessMeter(
  MIC_OUT, MIC_GAIN, MIC_SAMPLE_WINDOW,
  DEFAULT_P2P_LOW, DEFAULT_P2P_HIGH,
  DEFAULT_RMS_LOW, DEFAULT_RMS_HIGH);
uint16_t mappedSignal;

// Bluetooth
#include "BluetoothElectronics.h"
#define DEVICE_NAME "LOLIN32 Lite"
BluetoothElectronics bluetooth = BluetoothElectronics(DEVICE_NAME);

// EL Sequencer
#include "ELSequencer.h"
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
      periodicFlashWithDecay();
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
const Mode modes[] = {
  { "rPulse", ModeType::Reactive, reactivePulse, nullptr },
  { "rPulseDecay", ModeType::Reactive, reactivePulseWithDecay, nullptr },
  { "rBeatPulseDecay", ModeType::Reactive, reactiveBeatPulseDecay, nullptr },
  { "rRandom", ModeType::Reactive, reactiveRandomSimple, nullptr },
  { "rRandomSwap", ModeType::Reactive, reactiveRandomSwap, nullptr },
  { "rRandomHL", ModeType::Reactive, reactiveRandomHighLow, nullptr },
  { "rLinearSweep", ModeType::Reactive, reactiveLinearSweep, nullptr },
  { "pPulseUp", ModeType::Periodic, periodicPulseUp, nullptr },
  { "pPulseUpDown", ModeType::Periodic, periodicPulseUpDown, nullptr },
  { "pFlash", ModeType::Periodic, periodicFlash, nullptr },
  { "pFlashDecay", ModeType::Periodic, periodicFlashWithDecay, nullptr },
  { "pRandom", ModeType::Periodic, periodicRandom, nullptr },
};

uint8_t getModeCount() {
  return (uint8_t)(sizeof(modes) / sizeof(modes[0]));
}

bool isReactive(uint8_t idx) {
  if (idx >= getModeCount()) return false;
  return modes[idx].type == ModeType::Reactive;
}

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

uint16_t beatDisplayLevel = 0;
uint32_t beatLastDecayMs = 0;
void reactiveBeatPulseDecay() {
  const uint8_t THRESHOLD = 6;

  if (mappedSignal >= THRESHOLD && mappedSignal > beatDisplayLevel) {
    beatDisplayLevel = mappedSignal;
    beatLastDecayMs = millis();
  } else {
    if (beatDisplayLevel > 0) {
      uint32_t now = millis();
      uint16_t releaseMs = currentDelay();
      if (releaseMs == 0) {
        releaseMs = MIC_SAMPLE_WINDOW;
      }
      if (now - beatLastDecayMs >= releaseMs) {
        beatDisplayLevel -= 1;
        beatLastDecayMs = now;
      }
    }
  }

  sequencer.lightNumWiresUpToWire(numWires, beatDisplayLevel);
}

void reactiveRandomSimple() {
  static uint16_t last = 0;
  bool rising = mappedSignal > last;
  last = mappedSignal;
  if (rising && mappedSignal > 6) {
    sequencer.lightNumRandomWires(numWires);
  }
}

void reactiveRandomSwap() {
  static uint16_t last = 0;
  static uint8_t lastNumWires = 0;
  
  bool rising = mappedSignal > last;
  last = mappedSignal;
  
  if (rising && mappedSignal > 6) {
    // Get current pattern
    uint8_t pattern[ACTIVE_CHANNELS];
    sequencer.getCurrentPattern(pattern);
    
    // Count lit wires
    uint8_t litCount = 0;
    for (uint8_t i = 0; i < ACTIVE_CHANNELS; i++) {
      if (pattern[i] == 1) litCount++;
    }
    
    // If wrong number of wires lit or numWires changed, start fresh
    if (litCount != numWires || lastNumWires != numWires) {
      sequencer.lightNumRandomWires(numWires);
      lastNumWires = numWires;
      return;
    }
    
    // Calculate how many to swap based on available wires
    uint8_t darkCount = ACTIVE_CHANNELS - litCount;
    uint8_t swapCount = (litCount >= 2 && darkCount >= 2) ? 2 : 1;
    
    // Pick positions to turn on (from dark wires)
    uint8_t newOn[2];
    for (uint8_t i = 0; i < swapCount; i++) {
      newOn[i] = random(ACTIVE_CHANNELS);
      uint8_t attempts = 0;
      while (pattern[newOn[i]] == 1 && attempts++ < ACTIVE_CHANNELS) {
        newOn[i] = (newOn[i] + 1) % ACTIVE_CHANNELS;
      }
      // Mark as occupied so next one doesn't pick it
      if (i == 0 && swapCount == 2) pattern[newOn[i]] = 2;
    }
    if (swapCount == 2) pattern[newOn[0]] = 1; // Restore original value
    
    // Pick positions to turn off (from lit wires)
    uint8_t newOff[2];
    for (uint8_t i = 0; i < swapCount; i++) {
      newOff[i] = random(ACTIVE_CHANNELS);
      uint8_t attempts = 0;
      while (pattern[newOff[i]] == 0 && attempts++ < ACTIVE_CHANNELS) {
        newOff[i] = (newOff[i] + 1) % ACTIVE_CHANNELS;
      }
      // Mark as occupied so next one doesn't pick it
      if (i == 0 && swapCount == 2) pattern[newOff[i]] = 0;
    }
    if (swapCount == 2) pattern[newOff[0]] = 1; // Restore original value
    
    // Apply changes
    for (uint8_t i = 0; i < swapCount; i++) {
      pattern[newOn[i]] = 1;
      pattern[newOff[i]] = 0;
    }
    
    // Set the new pattern
    sequencer.lightWiresByPattern(pattern);
  }
}

void reactiveRandomHighLow() {
  static uint16_t last = 0;
  static uint32_t lastHighMs = 0;

  const uint8_t TH_MED = 2;
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

void reactiveLinearSweep() {
  static uint16_t last = 0;
  static uint8_t startIndex = 0;

  const uint8_t THRESHOLD = 6;

  uint16_t cur = mappedSignal;
  bool rising = cur > last;
  last = cur;

  if (!rising || cur <= THRESHOLD) return;

  uint8_t pattern[ACTIVE_CHANNELS];
  for (uint8_t i = 0; i < ACTIVE_CHANNELS; i++) {
    pattern[i] = 0;
  }

  for (uint8_t k = 0; k < numWires; k++) {
    uint8_t idx = (startIndex + k) % ACTIVE_CHANNELS;
    pattern[idx] = 1;
  }

  startIndex = (startIndex + 1) % ACTIVE_CHANNELS;

  sequencer.lightWiresByPattern(pattern);
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
  return periodicModeDelays[currentDelayIndex];
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
