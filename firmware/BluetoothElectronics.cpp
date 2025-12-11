#define DEBUG 0
#define DEBUG_BAUD_RATE 57600
#define DEBUG_INPUT 0

#include "BluetoothElectronics.h"
#if !DEBUG_INPUT
#include "BluetoothSerial.h"
#endif

BluetoothElectronics::BluetoothElectronics(String deviceName)
  : deviceName(deviceName) {}

void BluetoothElectronics::registerCommand(const String& receiveChar, void (*action)(const String&)) {
  Command* newCommand = new Command{ receiveChar, action, nullptr };
  if (!commandHead) {
    commandHead = newCommand;
  } else {
    Command* temp = commandHead;
    while (temp->next) {
      temp = temp->next;
    }
    temp->next = newCommand;
  }
}

void BluetoothElectronics::begin() {
#if DEBUG
  Serial.begin(DEBUG_BAUD_RATE);
#endif
#if DEBUG_INPUT
  if (!Serial) {
    Serial.begin(DEBUG_BAUD_RATE);
  }
  Serial.println("Using Serial for input (DEBUG_INPUT mode)");
#else
  serialBT.begin(deviceName, false);
#endif
}

void BluetoothElectronics::handleInput() {
  static String inputBuffer = "";
#if DEBUG_INPUT
  while (Serial.available()) {
    char c = Serial.read();
#else
  while (serialBT.available()) {
    char c = serialBT.read();
#endif
#if DEBUG
    Serial.println("Received char: " + String(c));
#endif
    if (c == '\n') {
      inputBuffer.trim();
#if DEBUG
      Serial.println("Received: " + inputBuffer);
#endif
      processInput(inputBuffer);
#if DEBUG_INPUT
      Serial.println("Echo: " + inputBuffer);
#else
  #if DEBUG
      serialBT.println("Echo: " + inputBuffer);
  #endif
#endif
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }
}

void BluetoothElectronics::processInput(String input) {
#if DEBUG
  Serial.println("Processing trimmed input: " + input);
#endif
  Command* current = commandHead;
  while (current) {
    if (input.startsWith(current->receiveChar)) {
#if DEBUG
      Serial.println("Matched receiveChar: " + current->receiveChar);
#endif
      String parameter = "";
      int receiveCharLength = current->receiveChar.length();
      if (input.length() > receiveCharLength) {
        parameter = input.substring(receiveCharLength);
      }
#if DEBUG
      Serial.println("Parameter: " + parameter);
#endif
      current->action(parameter);
      break;
    }
    current = current->next;
  }
#if DEBUG
  Serial.println("Finished processing input.");
#endif
}

void BluetoothElectronics::sendKwlString(String value, String receiveChar) {
  String cmd = "*" + receiveChar + value + "*";
#if DEBUG
  Serial.println("Sending: " + cmd);
#endif
#if DEBUG_INPUT
  Serial.println(cmd);
#else
  //serialBT.flush();
  serialBT.println(cmd);
#endif
}

void BluetoothElectronics::sendKwlValue(int value, String receiveChar) {
  sendKwlString(String(value), receiveChar);
}

void BluetoothElectronics::sendKwlCode(String code) {
  String cmd = String(KWL_BEGIN) + "\n" + code + "\n" + String(KWL_END);
#if DEBUG
  Serial.println("Sending: " + cmd);
#endif
#if DEBUG_INPUT
  Serial.print(cmd);
#else
  //serialBT.flush();
  serialBT.print(cmd);
#endif
}
