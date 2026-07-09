#include "jpeg_decoder.h"

namespace {
constexpr uint16_t COLOR_BLACK = 0x0000;

JpegDecoder* activeDecoder = nullptr;

int scaleDivisorFromOption(int option) {
#ifdef JPEG_SCALE_EIGHTH
    if (option == JPEG_SCALE_EIGHTH) {
        return 8;
    }
#endif
#ifdef JPEG_SCALE_QUARTER
    if (option == JPEG_SCALE_QUARTER) {
        return 4;
    }
#endif
#ifdef JPEG_SCALE_HALF
    if (option == JPEG_SCALE_HALF) {
        return 2;
    }
#endif
    return 1;
}
}  // namespace

bool JpegDecoder::begin() {
    _displayW = M5.Display.width() > 0 ? M5.Display.width() : DISPLAY_WIDTH;
    _displayH = M5.Display.height() > 0 ? M5.Display.height() : DISPLAY_HEIGHT;
    _lastDecodeMs = 0;
    _lastWidth = 0;
    _lastHeight = 0;
    _lastError = "ok";
    return true;
}

bool JpegDecoder::ensureFrameSprite(int w, int h) {
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (_frameReady && _frameW == w && _frameH == h) {
        return true;
    }
    if (_frameReady) {
        _frame.deleteSprite();
        _frameReady = false;
    }
    _frame.setPsram(true);
    _frame.setColorDepth(16);
    if (_frame.createSprite(w, h) == nullptr) {
        Serial.printf("JPEG decoder: frame sprite alloc failed (%dx%d); direct draw\n", w, h);
        _frameW = 0;
        _frameH = 0;
        return false;
    }
    _frameW = w;
    _frameH = h;
    _frameReady = true;
    return true;
}

bool JpegDecoder::drawFrame(LovyanGFX* dst, const uint8_t* data, size_t length) {
    if (data == nullptr || length < 4) {
        return setError("empty jpeg frame");
    }
    if (length > static_cast<size_t>(INT32_MAX)) {
        return setError("jpeg frame too large");
    }

    LovyanGFX* canvas = dst != nullptr ? dst : &M5.Display;
    const uint32_t started = millis();
    activeDecoder = this;

    if (!_jpeg.openRAM(const_cast<uint8_t*>(data), static_cast<int>(length), JpegDecoder::jpegDrawCallback)) {
        activeDecoder = nullptr;
        return setError("JPEG openRAM failed");
    }
    // M5GFX pushImage() expects byte-swapped RGB565 when Display.getSwapBytes() is false.
    // JPEGDEC defaults to little-endian RGB565, which produces psychedelic/garbled colors
    // on the StickS3 LCD. Output big-endian pixels directly for the SPI LCD path.
    _jpeg.setPixelType(RGB565_BIG_ENDIAN);

    _lastWidth = _jpeg.getWidth();
    _lastHeight = _jpeg.getHeight();

    const int scale = JPEG_SCALE_POLICY;
    const int divisor = scaleDivisorFromOption(scale);
    const int scaledW = (_lastWidth + divisor - 1) / divisor;
    const int scaledH = (_lastHeight + divisor - 1) / divisor;

    const int canvasW = canvas->width() > 0 ? canvas->width() : _displayW;
    const int canvasH = canvas->height() > 0 ? canvas->height() : _displayH;

    // Preferred path: decode into an off-screen sprite sized exactly to the
    // scaled image, then push it to the canvas scaled to *fit* (contain) the
    // screen. The whole uncropped frame is shown, as large as possible; a 4:3
    // frame on the 16:9 LCD fills the full height with thin black side bars.
    const bool useSprite = ensureFrameSprite(scaledW, scaledH);
    int rc = 0;
    if (useSprite) {
        _dst = &_frame;
        _dst->startWrite();
        rc = _jpeg.decode(0, 0, scale);
        _dst->endWrite();
    } else {
        // Fallback: decode centered directly onto the canvas (may letterbox).
        _dst = canvas;
        _drawX = (canvasW - scaledW) / 2;
        _drawY = (canvasH - scaledH) / 2;
        canvas->fillScreen(COLOR_BLACK);
        canvas->startWrite();
        rc = _jpeg.decode(_drawX, _drawY, scale);
        canvas->endWrite();
    }
    _jpeg.close();
    activeDecoder = nullptr;

    _lastDecodeMs = millis() - started;
    if (rc == 0) {
        return setError("JPEG decode failed");
    }

    if (useSprite) {
        // Contain-fit: pick the smaller of the width/height ratios so the whole
        // frame stays visible; the other axis is letterboxed. Clear the canvas
        // first so the uncovered bars are black rather than stale pixels.
        const float zoomX = static_cast<float>(canvasW) / static_cast<float>(scaledW);
        const float zoomY = static_cast<float>(canvasH) / static_cast<float>(scaledH);
        const float zoom = zoomX < zoomY ? zoomX : zoomY;
        canvas->fillScreen(COLOR_BLACK);
        _frame.setPivot(static_cast<float>(scaledW) / 2.0f, static_cast<float>(scaledH) / 2.0f);
        _frame.pushRotateZoom(canvas,
                              static_cast<float>(canvasW) / 2.0f,
                              static_cast<float>(canvasH) / 2.0f,
                              _rotationDeg, zoom, zoom);
    }

    _lastError = "ok";
    return true;
}

uint32_t JpegDecoder::lastDecodeMs() const {
    return _lastDecodeMs;
}

int JpegDecoder::lastWidth() const {
    return _lastWidth;
}

int JpegDecoder::lastHeight() const {
    return _lastHeight;
}

const String& JpegDecoder::lastError() const {
    return _lastError;
}

bool JpegDecoder::setError(const char* error) {
    _lastError = error != nullptr ? error : "unknown error";
    Serial.print(F("JPEG decoder error: "));
    Serial.println(_lastError);
    return false;
}

int JpegDecoder::jpegDrawCallback(JPEGDRAW* draw) {
    if (activeDecoder == nullptr || draw == nullptr) {
        return 0;
    }
    return activeDecoder->drawBlock(draw);
}

int JpegDecoder::drawBlock(JPEGDRAW* draw) {
    int16_t dstX = static_cast<int16_t>(draw->x);
    int16_t dstY = static_cast<int16_t>(draw->y);
    int16_t srcX = 0;
    int16_t srcY = 0;
    int16_t drawW = static_cast<int16_t>(draw->iWidth);
    int16_t drawH = static_cast<int16_t>(draw->iHeight);

    // Clip against the active draw target (the off-screen sprite while decoding,
    // or the display canvas in the fallback path), not a fixed display size.
    const int16_t targetW = static_cast<int16_t>(_dst->width());
    const int16_t targetH = static_cast<int16_t>(_dst->height());

    if (dstX < 0) {
        srcX = -dstX;
        drawW -= srcX;
        dstX = 0;
    }
    if (dstY < 0) {
        srcY = -dstY;
        drawH -= srcY;
        dstY = 0;
    }
    if (dstX + drawW > targetW) {
        drawW = targetW - dstX;
    }
    if (dstY + drawH > targetH) {
        drawH = targetH - dstY;
    }
    if (drawW <= 0 || drawH <= 0) {
        return 1;
    }

    uint16_t* pixels = static_cast<uint16_t*>(draw->pPixels);
    const int stride = draw->iWidth;

    if (srcX == 0 && drawW == draw->iWidth) {
        _dst->pushImage(dstX, dstY, drawW, drawH, pixels + (srcY * stride));
    } else {
        for (int16_t row = 0; row < drawH; ++row) {
            _dst->pushImage(dstX, dstY + row, drawW, 1, pixels + ((srcY + row) * stride) + srcX);
        }
    }
    return 1;
}