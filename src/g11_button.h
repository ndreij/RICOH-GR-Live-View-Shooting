#pragma once

#include <Arduino.h>

struct G11ButtonEvents {
  bool shortPress = false;
  bool longPress = false;
  bool any = false;
};

class G11Button {
public:
  void begin();
  G11ButtonEvents poll();

private:
  bool _stablePressed = false;
  bool _lastRawPressed = false;
  bool _longPressSent = false;
  uint32_t _lastRawChangeMs = 0;
  uint32_t _pressedAtMs = 0;
};