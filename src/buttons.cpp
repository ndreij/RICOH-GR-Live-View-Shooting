#include "buttons.h"

void Buttons::begin() {
    M5.update();
}

ButtonEvents Buttons::poll() {
    M5.update();

    ButtonEvents events;

    if (M5.BtnA.wasPressed()) {
        events.buttonA = true;
        events.buttonAReservedPressed = true;
        events.any = true;
        Serial.println(F("Button A reserved; shutter request intentionally not implemented"));
    }

    if (M5.BtnB.wasPressed()) {
        events.buttonB = true;
        events.buttonBToggleLiveview = true;
        events.liveviewToggle = true;
        events.any = true;
        Serial.println(F("Button B liveview toggle"));
    }

    return events;
}