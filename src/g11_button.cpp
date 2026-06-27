#include "g11_button.h"

#include "config.h"

void G11Button::begin() {
  pinMode(static_cast<uint8_t>(G11_BUTTON_GPIO), INPUT_PULLUP);
  const bool pressed = digitalRead(static_cast<uint8_t>(G11_BUTTON_GPIO)) == LOW;
  _stablePressed = pressed;
  _lastRawPressed = pressed;
  _lastRawChangeMs = millis();
  _pressedAtMs = pressed ? _lastRawChangeMs : 0;
  _longPressSent = false;
}

G11ButtonEvents G11Button::poll() {
  G11ButtonEvents events;
  const uint32_t now = millis();
  const bool rawPressed = digitalRead(static_cast<uint8_t>(G11_BUTTON_GPIO)) == LOW;

  if (rawPressed != _lastRawPressed) {
    _lastRawPressed = rawPressed;
    _lastRawChangeMs = now;
  }

  if ((now - _lastRawChangeMs) < G11_DEBOUNCE_MS) {
    return events;
  }

  if (rawPressed != _stablePressed) {
    _stablePressed = rawPressed;
    if (_stablePressed) {
      _pressedAtMs = now;
      _longPressSent = false;
    } else {
      if (!_longPressSent && _pressedAtMs > 0) {
        events.shortPress = true;
        events.any = true;
      }
      _pressedAtMs = 0;
      _longPressSent = false;
    }
  }

  if (_stablePressed && !_longPressSent && _pressedAtMs > 0 && (now - _pressedAtMs) >= G11_LONG_PRESS_MS) {
    _longPressSent = true;
    events.longPress = true;
    events.any = true;
  }

  return events;
}