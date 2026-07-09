#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <JPEGDEC.h>

#if __has_include("config.h")
#include "config.h"
#endif

#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 240
#endif

#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT 135
#endif

#ifndef JPEG_SCALE_POLICY
#define JPEG_SCALE_POLICY JPEG_SCALE_QUARTER
#endif

class JpegDecoder {
public:
    bool begin();
    bool drawFrame(LovyanGFX* dst, const uint8_t* data, size_t length);

    uint32_t lastDecodeMs() const;
    int lastWidth() const;
    int lastHeight() const;
    const String& lastError() const;

    // Rotation applied to the preview when pushing it to the screen, in degrees.
    // Driven by the StickS3 IMU so the image stays upright when the device is
    // flipped 180 degrees (see main.cpp orientation polling). Only 0 and 180 are
    // used today; both preserve the frame aspect so the fit math is unaffected.
    void setRotationDegrees(float deg) { _rotationDeg = deg; }

private:
    JPEGDEC _jpeg;
    LovyanGFX* _dst = nullptr;
    uint32_t _lastDecodeMs = 0;
    int _lastWidth = 0;
    int _lastHeight = 0;
    String _lastError = "not started";
    float _rotationDeg = 0.0f;

    int _drawX = 0;
    int _drawY = 0;
    int _displayW = DISPLAY_WIDTH;
    int _displayH = DISPLAY_HEIGHT;

    // Off-screen sprite the JPEG is decoded into at its (down)scaled size. It is
    // then pushed to the display canvas with zoom using a *contain* (fit) fit:
    // the whole uncropped frame is shown, scaled as large as possible until one
    // axis fills the screen. For a 4:3 camera frame on the 16:9 LCD this fills
    // the full height and leaves thin black bars left/right (nothing is cropped).
    // Falls back to decoding straight onto the canvas if the sprite cannot be
    // allocated.
    M5Canvas _frame;
    bool _frameReady = false;
    int _frameW = 0;
    int _frameH = 0;

    bool ensureFrameSprite(int w, int h);

    bool setError(const char* error);

    static int jpegDrawCallback(JPEGDRAW* draw);
    int drawBlock(JPEGDRAW* draw);
};