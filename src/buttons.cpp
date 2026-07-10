#include "buttons.h"

#include "config.h"

void Buttons::begin() {
  M5.update();
  M5.BtnPWR.setHoldThresh(POWER_BUTTON_HOLD_MS);
  pinMode(KEY2_FALLBACK_GPIO, INPUT_PULLUP);
}

ButtonEvents Buttons::poll() {
  M5.update();

  ButtonEvents events;
  if (M5.BtnPWR.wasHold()) {
    events.powerOff = true;
    events.any = true;
  }

  // Front button (BtnA): a short tap is the shutter / manual-wake trigger; the
  // same button held for BTNA_PAIRING_HOLD_MS starts a fresh pairing scan
  // (mirrors the KEY2 long-press below). The tap action is fired on RELEASE and
  // only when the press did NOT grow into a hold. Acting on the press edge
  // instead would fire a stray shutter/manual-wake at the START of every
  // hold-to-pair gesture -- on the camera-off screen that shows up as a brief
  // "Connecting..." flash before the pairing reset supersedes it, which is
  // exactly the flicker we want to avoid.
  const uint32_t now = millis();
  if (M5.BtnA.isPressed()) {
    if (_btnAPressedSince == 0) {
      _btnAPressedSince = now;
    }
    if (!_btnAHoldReported && (now - _btnAPressedSince) >= BTNA_PAIRING_HOLD_MS) {
      _btnAHoldReported = true;
      events.resetPairing = true;
      events.any = true;
    }
  }
  if (M5.BtnA.wasReleased()) {
    // Released before the hold threshold: a plain tap -> fire the shutter /
    // manual-wake action. If the hold already fired the pairing reset, the
    // release is a no-op so no stray connect attempt is triggered.
    if (!_btnAHoldReported) {
      events.buttonA = true;
      events.any = true;
    }
    _btnAPressedSince = 0;
    _btnAHoldReported = false;
  }

  const bool key2Down = key2Pressed();
  if (!key2Down) {
    _key2PressedSince = 0;
    _key2HoldReported = false;
  } else {
    if (_key2PressedSince == 0) {
      _key2PressedSince = now;
    }
    if (!_key2HoldReported && (now - _key2PressedSince) >= KEY2_PAIRING_RESET_HOLD_MS) {
      _key2HoldReported = true;
      events.resetPairing = true;
      events.any = true;
    }
  }
  return events;
}

bool Buttons::key2Pressed() const {
  return M5.BtnB.isPressed() || digitalRead(KEY2_FALLBACK_GPIO) == LOW;
}
