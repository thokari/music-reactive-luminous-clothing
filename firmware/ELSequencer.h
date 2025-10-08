#ifndef EL_SEQUENCER_H
#define EL_SEQUENCER_H

#include "Arduino.h"

class ELSequencer {
public:
  ELSequencer(const uint8_t order[], const uint8_t count);
  void begin();
  void lightNumWires(uint8_t num);
  void lightWiresAtIndex(uint8_t index);
  void lightNumWiresUpToWire(uint8_t num, uint8_t wireNum);
  void lightWiresByPattern(uint8_t pattern[]);
  void lightAll();
  void lightNone();
  void lightRandomWires();
  void lightNumRandomWires(uint8_t num);

private:
  void initSequencer();
  void playWireStartSequence();
  const uint8_t channelCount;
  const uint8_t* channelOrder;
  uint8_t* channelIndices;
};

#endif