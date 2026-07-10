#pragma once

#include <Arduino.h>
#include <M5Unified.h>

struct ButtonEvents {
  bool buttonA = false;
  bool resetPairing = false;
  bool powerOff = false;
  bool any = false;
};

class Buttons {
public:
  void begin();
  ButtonEvents poll();

private:
  bool key2Pressed() const;

  uint32_t _key2PressedSince = 0;
  bool _key2HoldReported = false;
  uint32_t _btnAPressedSince = 0;
  bool _btnAHoldReported = false;
};
