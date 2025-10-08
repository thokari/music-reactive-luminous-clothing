#ifndef BLUETOOTH_ELECTRONICS_H
#define BLUETOOTH_ELECTRONICS_H

#include "Arduino.h"
#include "BluetoothSerial.h"

#define KWL_BEGIN "*.kwl"
#define KWL_END "*"

class BluetoothElectronics {
public:
  BluetoothElectronics(String deviceName);
  void registerCommand(const String& receiveChar, void (*action)(const String&));
  void begin();
  void handleInput();

  void sendKwlString(String input, String receiveChar);
  void sendKwlValue(int value, String receiveChar);
  void sendKwlCode(String code);

private:
  struct Command {
    String receiveChar;
    void (*action)(const String& parameter);
    Command* next = nullptr;

    Command(const String& r, void (*a)(const String&), Command* n = nullptr) : receiveChar(r), action(a), next(n) {}
  };

  String deviceName;
  BluetoothSerial serialBT;
  Command* commandHead = nullptr;
  void processInput(String input);
};

#endif