#define DEBUG 0
#define DEBUG_BAUD_RATE 57600

// LoudnessMeter
#include "LoudnessMeter.h"
#define MIC_OUT 35
#define MIC_GAIN 32
#define MIC_SAMPLE_WINDOW 16  // ms
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
#define DEVICE_NAME "LOLIN32 Lite"
BluetoothElectronics bluetooth = BluetoothElectronics(DEVICE_NAME);

// EL Sequencer
#include "ELSequencer.h"
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
  CHANNEL_D, CHANNEL_C, CHANNEL_B, CHANNEL_A, CHANNEL_H, CHANNEL_G, CHANNEL_F, CHANNEL_E
};
ELSequencer sequencer = ELSequencer(channelOrder, ACTIVE_CHANNELS);
#define NUM_MODES 8
uint8_t mode = 0;
uint8_t numWires = 8;
#define NUM_DELAYS 10
uint16_t fixedModeDelays[NUM_DELAYS] = { 10, 25, 33, 50, 66, 100, 166, 250, 500, 1000 };
uint8_t currentDelayIndex = 1;
uint32_t timer = 0;
#define ADDITIONAL_GND_PIN 18;

// Push-Buttons
#define BUTTON_1_PIN 25
#define DEBOUNCE_MS 5
#define BUTTON_PAUSE_MS 1000
volatile bool button1Pressed = false;
volatile bool skipLoop = false;
volatile unsigned long lastButtonPressTime = 0;
volatile unsigned long lastEdgeTime = 0;

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
  initPushButtons();
  sequencer.begin();
  pinMode(ADDITIONAL_GND_PIN, INPUT);
#if DEBUG
  Serial.println("Setup complete");
#endif
}

boolean outputToBluetooth = false;
uint32_t loopBegin = 0;

void loop() {
  loopBegin = millis();
  if (loopBegin - lastButtonPressTime > BUTTON_PAUSE_MS) {
    skipLoop = false;
  }
  if (skipLoop) {
    if (button1Pressed) {
      runFixedPatternMode_4();
      button1Pressed = false;
    }
    return;
  }
  bluetooth.handleInput();
  if (mode == 0 || mode == 1 || mode == 2) {
    mic.readAudioSample();
    processSample();
    if (mode == 0) {
      runReactiveMode_1();
    } else if (mode == 1) {
      runReactiveMode_2();
    } else if (mode == 2) {
      runReactiveMode_3();
    }
    if (outputToBluetooth) {
      printToBluetooth();
    }
#if DEBUG
    printToSerialMonitor();
#endif
  } else if (mode == 3) {
    runFixedPatternMode_1();
  } else if (mode == 4) {
    runFixedPatternMode_2();
  } else if (mode == 5) {
    runFixedPatternMode_3();
  } else if (mode == 6) {
    runFixedPatternMode_4();
  } else if (mode == 7) {
    runFixedPatternMode_5();
  }
}

void IRAM_ATTR handleButton1FallingInterrupt() {
  unsigned long currentEdgeTime = millis();
  if (currentEdgeTime - lastEdgeTime > DEBOUNCE_MS) {
    button1Pressed = true;
    lastButtonPressTime = currentEdgeTime;
  }
  lastEdgeTime = currentEdgeTime;
}

void IRAM_ATTR handleRisingInterrupt() {
  lastEdgeTime = millis();
}

void initPushButtons() {
  pinMode(BUTTON_1_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_1_PIN), handleButton1FallingInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_1_PIN), handleRisingInterrupt, RISING);
}

void runReactiveMode_1() {
  if (numWires == 1) {
    mappedSignal > 0 ? sequencer.lightWiresAtIndex(mappedSignal - 1) : sequencer.lightNumWires(0);
  } else if (numWires == ACTIVE_CHANNELS) {
    sequencer.lightNumWires(mappedSignal);
  } else {
    sequencer.lightNumWiresUpToWire(numWires, mappedSignal);
  }
}

uint8_t counter;
void runReactiveMode_2() {
  if (mappedSignal > 6) {
    if (counter == 1) {
      uint8_t pattern[ACTIVE_CHANNELS] = { 0, 1, 0, 1, 0, 1, 0, 1 };
      sequencer.lightWiresByPattern(pattern);
    } else {
      uint8_t pattern[ACTIVE_CHANNELS] = { 1, 0, 1, 0, 1, 0, 1, 0 };
      sequencer.lightWiresByPattern(pattern);
    }
    counter = counter == 0 ? 1 : 0;
  }
}

void runReactiveMode_3() {
  if (mappedSignal > 6) {
    sequencer.lightNumRandomWires(numWires);
  }
}

void runFixedPatternMode_1() {
  for (int i = 0; i <= ACTIVE_CHANNELS; i++) {
    sequencer.lightWiresAtIndex(i);
    delay(currentDelay());
  }
}

void runFixedPatternMode_2() {
  for (int i = 0; i <= ACTIVE_CHANNELS - 1; i++) {
    sequencer.lightWiresAtIndex(i);
    delay(currentDelay());
  }
  for (int i = ACTIVE_CHANNELS - 2; i >= 1; i--) {
    sequencer.lightWiresAtIndex(i);
    delay(currentDelay());
  }
}

void runFixedPatternMode_3() {
  sequencer.lightAll();
  delay(currentDelay());
  sequencer.lightNone();
  delay(currentDelay());
}

void runFixedPatternMode_4() {
  sequencer.lightAll();
  delay(currentDelay());
  for (int i = ACTIVE_CHANNELS - 1; i >= 0; i--) {
    sequencer.lightNumWires(i);
    delay(currentDelay());
  }
}

void runFixedPatternMode_5() {
  sequencer.lightRandomWires();
  delay(currentDelay());
}

void printToSerialMonitor() {
  Serial.print(mic.getLow());
  Serial.print(",");
  Serial.print(mic.getHigh());
  Serial.print(",");
  Serial.print(millis() - loopBegin);
  Serial.println();
}

void printToBluetooth() {
  String data = String(mic.getSignal()) + "," + String(mic.getLow()) + "," + String(mic.getHigh());
  bluetooth.sendKwlString(data, "G");
}

void processSample() {
#if DEBUG
  Serial.print(mic.getSignal());
  Serial.print(",");
#endif
  uint16_t contrainedSignal = constrain(mic.getSignal(), mic.getLow(), mic.getHigh());
  mappedSignal = map(contrainedSignal, mic.getLow(), mic.getHigh(), 0, ACTIVE_CHANNELS);
}

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

void cmdSetLow(const String& parameter) {
  mic.setLow(parameter.toInt());
  bluetooth.sendKwlValue(mic.getLow(), "L");
}

void cmdSetHigh(const String& parameter) {
  mic.setHigh(parameter.toInt());
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
  mode = (mode + 1) % NUM_MODES;
  printMode();
}

void prevMode() {
  if (mode == 0) {
    mode = NUM_MODES - 1;
  } else {
    mode--;
  }
  printMode();
}

void nextSetting() {
  if (mode < 3) {
    if (++numWires > ACTIVE_CHANNELS) {
      numWires = 1;
    }
    bluetooth.sendKwlValue(numWires, "S");
  } else if (mode >= 3) {
    currentDelayIndex = (currentDelayIndex + 1) % NUM_DELAYS;
    bluetooth.sendKwlValue(currentDelay(), "S");
  }
}

void prevSetting() {
  if (mode < 3) {
    numWires = numWires - 1;
    if (numWires == 0) {
      numWires = ACTIVE_CHANNELS;
    }
    bluetooth.sendKwlValue(numWires, "S");
  } else if (mode >= 3) {
    if (currentDelayIndex == 0) {
      currentDelayIndex = NUM_DELAYS - 1;
    } else {
      currentDelayIndex--;
    }
    bluetooth.sendKwlValue(currentDelay(), "S");
  }
}

void printMode() {
  if (mode == 0) {
    bluetooth.sendKwlString("R1", "M");
    bluetooth.sendKwlValue(numWires, "S");
  } else if (mode == 1) {
    bluetooth.sendKwlString("R2", "M");
    bluetooth.sendKwlValue(numWires, "S");
   } else if (mode == 2) {
    bluetooth.sendKwlString("R3", "M");
    bluetooth.sendKwlValue(numWires, "S");
  } else if (mode == 3) {
    bluetooth.sendKwlString("F1", "M");
    bluetooth.sendKwlValue(currentDelay(), "S");
  } else if (mode == 4) {
    bluetooth.sendKwlString("F2", "M");
    bluetooth.sendKwlValue(currentDelay(), "S");
  } else if (mode == 5) {
    bluetooth.sendKwlString("F3", "M");
    bluetooth.sendKwlValue(currentDelay(), "S");
  } else if (mode == 6) {
    bluetooth.sendKwlString("F4", "M");
    bluetooth.sendKwlValue(currentDelay(), "S");
  } else if (mode == 7) {
    bluetooth.sendKwlString("F5", "M");
    bluetooth.sendKwlValue(currentDelay(), "S");
  }
}

uint16_t currentDelay() {
  return fixedModeDelays[currentDelayIndex];
}
