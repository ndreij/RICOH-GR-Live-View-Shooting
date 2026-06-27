#include "display.h"

namespace {
constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_YELLOW = 0xFFE0;
constexpr uint16_t COLOR_CYAN = 0x07FF;
constexpr uint16_t COLOR_DARK = 0x18E3;

const char* safeText(const char* value, const char* fallback = "") {
    return value != nullptr ? value : fallback;
}
}  // namespace

bool DisplayUi::begin() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    _width = M5.Display.width();
    _height = M5.Display.height();

    // StickS3 is physically 135x240; rotation 1/3 should expose landscape 240x135.
    // If a board package reports the opposite, rotate once more to keep callers in landscape.
    if (_width < _height) {
        M5.Display.setRotation(3);
        _width = M5.Display.width();
        _height = M5.Display.height();
    }

    clear();
    M5.Display.setTextSize(1);
    M5.Display.setTextWrap(false);
    return true;
}

void DisplayUi::showBoot(const char* message) {
    clear();
    drawHeader("RICOH GR", COLOR_CYAN);
    M5.Display.setTextColor(COLOR_WHITE, COLOR_BLACK);
    M5.Display.setCursor(10, 42);
    M5.Display.print("StickS3 Viewfinder");
    M5.Display.setCursor(10, 62);
    M5.Display.print(safeText(message, "Booting..."));
    M5.Display.setCursor(10, _height - 18);
    M5.Display.setTextColor(COLOR_YELLOW, COLOR_BLACK);
    M5.Display.print("BtnB: liveview  BtnA: reserved");
}

void DisplayUi::showStatus(const char* line1, const char* line2, const char* line3, float fps) {
    clear();
    drawHeader("STATUS", COLOR_GREEN);
    drawStatusLines(line1, line2, line3);
    drawOverlay(fps, "status", false);
}

void DisplayUi::showStatus(const char* line1, const char* line2, const char* line3, const char* line4) {
    clear();
    drawHeader("STATUS", COLOR_GREEN);
    drawStatusLines(line1, line2, line3, line4);
    drawOverlay(-1.0f, "status", false);
}

void DisplayUi::showStatus(const String& line1, const String& line2, const String& line3, float fps) {
    showStatus(line1.c_str(), line2.length() ? line2.c_str() : nullptr, line3.length() ? line3.c_str() : nullptr, fps);
}

void DisplayUi::showStatus(const String& line1, const String& line2, const String& line3, const String& line4) {
    showStatus(line1.c_str(),
               line2.length() ? line2.c_str() : nullptr,
               line3.length() ? line3.c_str() : nullptr,
               line4.length() ? line4.c_str() : nullptr);
}

void DisplayUi::showError(const char* message, const char* detail) {
    clear();
    drawHeader("ERROR", COLOR_RED);
    M5.Display.setTextColor(COLOR_RED, COLOR_BLACK);
    M5.Display.setCursor(10, 36);
    M5.Display.print(safeText(message, "Unknown error"));
    if (detail != nullptr && detail[0] != '\0') {
        M5.Display.setTextColor(COLOR_WHITE, COLOR_BLACK);
        M5.Display.setCursor(10, 58);
        M5.Display.print(detail);
    }
    M5.Display.setTextColor(COLOR_YELLOW, COLOR_BLACK);
    M5.Display.setCursor(10, _height - 18);
    M5.Display.print("Press BtnB to reconnect");
}

void DisplayUi::showError(const String& message, const String& detail) {
    showError(message.c_str(), detail.length() ? detail.c_str() : nullptr);
}

void DisplayUi::drawOverlay(float fps, const char* status, bool liveviewActive) {
    char fpsText[24];
    if (fps >= 0.0f) {
        snprintf(fpsText, sizeof(fpsText), "%.1f FPS", static_cast<double>(fps));
    } else {
        snprintf(fpsText, sizeof(fpsText), "-- FPS");
    }

    const int16_t barH = 18;
    M5.Display.fillRect(0, 0, _width, barH, COLOR_DARK);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(liveviewActive ? COLOR_GREEN : COLOR_YELLOW, COLOR_DARK);
    M5.Display.setCursor(4, 5);
    M5.Display.print(liveviewActive ? "LIVE" : "IDLE");

    M5.Display.setTextColor(COLOR_WHITE, COLOR_DARK);
    M5.Display.setCursor(52, 5);
    M5.Display.print(fpsText);

    if (status != nullptr && status[0] != '\0') {
        M5.Display.setTextColor(COLOR_CYAN, COLOR_DARK);
        M5.Display.setCursor(132, 5);
        M5.Display.print(status);
    }
}

void DisplayUi::drawOverlay(float fps, const String& status, bool liveviewActive) {
    drawOverlay(fps, status.c_str(), liveviewActive);
}

void DisplayUi::drawOverlay(const String& wifiStatus,
                            const String& liveviewStatus,
                            const String& model,
                            const String& battery,
                            float fps,
                            int32_t rssi,
                            uint32_t frames,
                            uint32_t droppedFrames) {
    const bool liveviewActive = liveviewStatus == "LIVE";
    drawOverlay(fps, wifiStatus, liveviewActive);

    const int16_t footerY = _height - 16;
    M5.Display.fillRect(0, footerY, _width, 16, COLOR_DARK);
    M5.Display.setTextSize(1);

    M5.Display.setTextColor(COLOR_WHITE, COLOR_DARK);
    M5.Display.setCursor(4, footerY + 4);
    if (model.length() > 0) {
        M5.Display.print(model.substring(0, 10));
    } else {
        M5.Display.print("RICOH GR");
    }

    M5.Display.setTextColor(COLOR_CYAN, COLOR_DARK);
    M5.Display.setCursor(74, footerY + 4);
    if (battery.length() > 0) {
        M5.Display.print(battery.substring(0, 8));
    } else {
        M5.Display.print("BAT --");
    }

    M5.Display.setTextColor(COLOR_GREEN, COLOR_DARK);
    M5.Display.setCursor(128, footerY + 4);
    M5.Display.printf("%lddBm", static_cast<long>(rssi));

    M5.Display.setTextColor(droppedFrames == 0 ? COLOR_WHITE : COLOR_YELLOW, COLOR_DARK);
    M5.Display.setCursor(184, footerY + 4);
    M5.Display.printf("%lu/%lu",
                      static_cast<unsigned long>(frames),
                      static_cast<unsigned long>(droppedFrames));
}

int16_t DisplayUi::width() const {
    return _width;
}

int16_t DisplayUi::height() const {
    return _height;
}

void DisplayUi::clear(uint16_t color) {
    M5.Display.fillScreen(color);
}

void DisplayUi::drawHeader(const char* title, uint16_t color) {
    M5.Display.fillRect(0, 0, _width, 24, color);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COLOR_BLACK, color);
    M5.Display.setCursor(8, 8);
    M5.Display.print(title);
}

void DisplayUi::drawStatusLines(const char* line1, const char* line2, const char* line3, const char* line4) {
    const char* lines[] = {line1, line2, line3, line4};
    M5.Display.setTextColor(COLOR_WHITE, COLOR_BLACK);
    for (uint8_t i = 0; i < 4; ++i) {
        if (lines[i] != nullptr && lines[i][0] != '\0') {
            M5.Display.setCursor(10, 34 + (i * 20));
            M5.Display.print(lines[i]);
        }
    }
}