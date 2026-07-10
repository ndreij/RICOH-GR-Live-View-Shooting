#include "display.h"

namespace {
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_YELLOW = 0xFFE0;

// RICOH styling additions
constexpr uint16_t COLOR_BG = 0x1082;     // Matte Charcoal Black (#121212)
constexpr uint16_t COLOR_AMBER = 0xFD20;  // Signature Amber Orange (#FF9500)
constexpr uint16_t COLOR_SLATE = 0x2104;  // Slate Gray (#212121)
constexpr uint16_t COLOR_GRAY = 0x7BEF;   // Mid Gray (#7B7B7B)
constexpr uint16_t COLOR_CARD = 0x0841;   // Deep black panel
constexpr uint16_t COLOR_GRAPHITE = 0x31A6;

// Title shown on the boot / status screens. This build talks to whichever GR
// body pairs over BLE (GR IIIx, GR IV, ...) and the exact model isn't known
// until BLE/Wi-Fi identify it, so the status screen shows a generic product
// name here rather than guessing/hardcoding one specific model.
constexpr const char* kModelLabel = "GR REMOTE";

const char* safeText(const char* value, const char* fallback = "") {
    return value != nullptr ? value : fallback;
}


char upperAscii(char ch) {
    return (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - ('a' - 'A')) : ch;
}

bool containsIgnoreCase(const char* value, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') {
        return true;
    }
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    for (const char* pos = value; *pos != '\0'; ++pos) {
        const char* hay = pos;
        const char* pat = needle;
        while (*hay != '\0' && *pat != '\0' && upperAscii(*hay) == upperAscii(*pat)) {
            ++hay;
            ++pat;
        }
        if (*pat == '\0') {
            return true;
        }
    }
    return false;
}

bool statusContains(const char* line1,
                    const char* line2,
                    const char* line3,
                    const char* line4,
                    const char* needle) {
    return containsIgnoreCase(line1, needle) ||
           containsIgnoreCase(line2, needle) ||
           containsIgnoreCase(line3, needle) ||
           containsIgnoreCase(line4, needle);
}


}  // namespace

bool DisplayUi::begin() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    _baseRotation = 1;
    M5.Display.setRotation(_baseRotation);
    _width = M5.Display.width();
    _height = M5.Display.height();

    // StickS3 is physically 135x240; rotation 1/3 should expose landscape 240x135.
    // If a board package reports the opposite, rotate once more to keep callers in landscape.
    if (_width < _height) {
        _baseRotation = 3;
        M5.Display.setRotation(_baseRotation);
        _width = M5.Display.width();
        _height = M5.Display.height();
    }

    // Initialize Canvas sprite for double-buffered flicker-free rendering
    _canvas.setColorDepth(16); // 16-bit RGB565
    _canvas.createSprite(_width, _height);

    clear(COLOR_BG);
    _canvas.setTextSize(1);
    _canvas.setTextWrap(false);

    // Initial push to screen
    pushCanvas();
    return true;
}

void DisplayUi::setFlipped(bool flipped) {
    if (flipped == _flipped) {
        return;
    }
    _flipped = flipped;
    // +2 mod 4 is a 180 turn; keeps landscape orientation, just upside down.
    M5.Display.setRotation(flipped ? (_baseRotation ^ 2) : _baseRotation);
    pushCanvas();
}

void DisplayUi::showBoot(const char* message) {
    clear(COLOR_BG);

    // Outer card border (matching BLE status screen)
    const int16_t x = 8;
    const int16_t y = 6;
    const int16_t w = _width - 16;
    const int16_t h = _height - 12;
    _canvas.fillRoundRect(x, y, w, h, 10, COLOR_CARD);
    _canvas.drawRoundRect(x, y, w, h, 10, COLOR_GRAPHITE);
    _canvas.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 8, COLOR_SLATE);

    // Clean centered brand — no busy geometric artwork behind the text.
    _canvas.setTextColor(COLOR_AMBER, COLOR_CARD);
    _canvas.setTextSize(3);
    _canvas.setCursor(102, 20);
    _canvas.print("GR");

    // Subtitle
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_CARD);
    _canvas.setCursor(84, 52);
    _canvas.print("VIEWFINDER");

    // Progress bar matching the layout
    _canvas.drawRoundRect(40, 70, 160, 6, 3, COLOR_GRAPHITE);
    _canvas.fillRoundRect(42, 72, 60, 2, 1, COLOR_AMBER);

    // Dynamic boot status message
    _canvas.setTextColor(COLOR_GRAY);
    const char* msg = safeText(message, "Booting...");
    int msgLen = strlen(msg);
    _canvas.setCursor((240 - (msgLen * 6)) / 2, 86);
    _canvas.print(msg);

    // Footer divider and hotkeys hint
    _canvas.drawFastHLine(20, _height - 24, 200, COLOR_SLATE);
    _canvas.setCursor(60, _height - 16);
    _canvas.print("BtnA: Shutter / Retry");

    pushCanvas();
}

void DisplayUi::showStatus(const char* line1, const char* line2, const char* line3, const char* line4) {
    clear(COLOR_BG);
    drawStatusLines(line1, line2, line3, line4);
    pushCanvas();
}

void DisplayUi::showStatus(const String& line1, const String& line2, const String& line3, const String& line4) {
    showStatus(line1.c_str(),
               line2.length() ? line2.c_str() : nullptr,
               line3.length() ? line3.c_str() : nullptr,
               line4.length() ? line4.c_str() : nullptr);
}

void DisplayUi::showError(const char* message, const char* detail) {
    clear(COLOR_BG);

    // Header
    _canvas.drawFastHLine(10, 24, _width - 20, COLOR_SLATE);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_RED, COLOR_BG);
    _canvas.setCursor(10, 8);
    _canvas.print("SYSTEM ERROR");

    // Outlined error card
    _canvas.drawRoundRect(10, 32, 220, 78, 4, COLOR_RED);

    _canvas.setTextColor(COLOR_RED, COLOR_BG);
    _canvas.setCursor(20, 42);
    _canvas.print(safeText(message, "Unknown error"));

    if (detail != nullptr && detail[0] != '\0') {
        _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas.setCursor(20, 62);
        _canvas.print(detail);
    }

    // Footer action hint
    _canvas.drawFastHLine(10, _height - 20, _width - 20, COLOR_SLATE);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.setCursor(50, _height - 14);
    _canvas.print("Press BtnA to retry");

    pushCanvas();
}

void DisplayUi::showError(const String& message, const String& detail) {
    showError(message.c_str(), detail.length() ? detail.c_str() : nullptr);
}

void DisplayUi::showPasskeyEntry(const uint8_t digits[6], uint8_t pos) {
    clear(COLOR_BG);

    // Same row height/gap as the status screen's title/status/hint block, so
    // this screen's title -> hint spacing reads consistently with the rest of
    // the UI instead of using a one-off layout.
    constexpr int16_t kRowH = 16;   // textSize(2) glyph height
    constexpr int16_t kRowGap = 12;
    const int16_t titleY = 14;
    const int16_t hintY = static_cast<int16_t>(titleY + kRowH + kRowGap);

    // Title, same size as the status screen.
    _canvas.setTextSize(2);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    const char* title = "ENTER CODE";
    const int16_t titleW = static_cast<int16_t>(strlen(title) * 12);
    _canvas.setCursor((_width - titleW) / 2, titleY);
    _canvas.print(title);

    // One-line reminder of the button gesture, directly under the title: a
    // tap cycles the active digit through 0-9, a hold locks it in and
    // advances to the next one (see PASSKEY_ADVANCE_HOLD_MS). Same text size
    // as the title and every other screen (size 2) for visual consistency.
    _canvas.setTextSize(2);
    _canvas.setTextColor(COLOR_GRAY, COLOR_BG);
    const char* gestureHint = "TAP=NUM HOLD=NEXT";
    const int16_t gestureHintW = static_cast<int16_t>(strlen(gestureHint) * 12);
    _canvas.setCursor((_width - gestureHintW) / 2, hintY);
    _canvas.print(gestureHint);

    // Six digit cells, centered. Shifted down from the title/hint block above.
    const int16_t cellW = 30;
    const int16_t cellH = 34;
    const int16_t gap = 6;
    const int16_t totalW = 6 * cellW + 5 * gap;
    const int16_t startX = (_width - totalW) / 2;
    const int16_t y = static_cast<int16_t>(hintY + kRowH + kRowGap);

    _canvas.setTextSize(3);
    for (uint8_t i = 0; i < 6; ++i) {
        const int16_t cx = startX + i * (cellW + gap);
        const bool active = (i == pos);
        const bool committed = (i < pos);

        _canvas.fillRoundRect(cx, y, cellW, cellH, 4,
                              active ? COLOR_SLATE : COLOR_CARD);
        _canvas.drawRoundRect(cx, y, cellW, cellH, 4,
                              active ? COLOR_AMBER : COLOR_GRAPHITE);

        const uint16_t digitColor =
            active ? COLOR_AMBER : (committed ? COLOR_WHITE : COLOR_GRAY);
        _canvas.setTextColor(digitColor);
        // Center the single character (18px wide at size 3) in the cell.
        _canvas.setCursor(cx + (cellW - 18) / 2, y + (cellH - 24) / 2);
        _canvas.print(static_cast<char>('0' + (digits[i] % 10)));
    }

    pushCanvas();
}

// Redesigned transparent HUD overlay for Live Viewfinder (rendered onto _canvas)
void DisplayUi::drawOverlay(const String& wifiStatus,
                            const String& liveviewStatus,
                            const String& model,
                            const String& battery,
                            float fps,
                            int32_t rssi,
                            uint32_t frames,
                            uint32_t droppedFrames) {
    // 1. Draw corner crop marks of the viewport (Assuming standard 4:3 centered image width 180, X=30..210)
    const int16_t vx = 30;
    const int16_t vy = 0;
    const int16_t vw = 180;
    const int16_t vh = 135;
    const int16_t len = 8;

    // Top-Left corner
    _canvas.drawFastHLine(vx, vy, len, COLOR_WHITE);
    _canvas.drawFastVLine(vx, vy, len, COLOR_WHITE);
    // Top-Right corner
    _canvas.drawFastHLine(vx + vw - len, vy, len, COLOR_WHITE);
    _canvas.drawFastVLine(vx + vw - 1, vy, len, COLOR_WHITE);
    // Bottom-Left corner
    _canvas.drawFastHLine(vx, vy + vh - 1, len, COLOR_WHITE);
    _canvas.drawFastVLine(vx, vy + vh - len, len, COLOR_WHITE);
    // Bottom-Right corner
    _canvas.drawFastHLine(vx + vw - len, vy + vh - 1, len, COLOR_WHITE);
    _canvas.drawFastVLine(vx + vw - 1, vy + vh - len, len, COLOR_WHITE);

    // 2. Draw autofocus bracket in the center (green, X=108..132, Y=59..75)
    const int16_t cx = _width / 2;
    const int16_t cy = _height / 2;
    const int16_t bw = 12;
    const int16_t bh = 8;

    _canvas.drawFastVLine(cx - bw, cy - bh, bh * 2, COLOR_GREEN);
    _canvas.drawFastHLine(cx - bw, cy - bh, 4, COLOR_GREEN);
    _canvas.drawFastHLine(cx - bw, cy + bh - 1, 4, COLOR_GREEN);

    _canvas.drawFastVLine(cx + bw - 1, cy - bh, bh * 2, COLOR_GREEN);
    _canvas.drawFastHLine(cx + bw - 4, cy - bh, 4, COLOR_GREEN);
    _canvas.drawFastHLine(cx + bw - 4, cy + bh - 1, 4, COLOR_GREEN);

    // 3. Draw transparent HUD overlays
    _canvas.setTextSize(1);

    // Top-Left: LIVE state with a small status dot
    const bool liveActive = (liveviewStatus == "LIVE");
    _canvas.fillCircle(14, 10, 3, liveActive ? COLOR_GREEN : COLOR_YELLOW);
    _canvas.setTextColor(COLOR_WHITE);
    _canvas.setCursor(22, 6);
    _canvas.print(liveActive ? "LIVE" : "IDLE");

    // Top-Left (below LIVE): FPS display
    char fpsText[16];
    if (fps >= 0.0f) {
        snprintf(fpsText, sizeof(fpsText), "%.1f FPS", static_cast<double>(fps));
    } else {
        snprintf(fpsText, sizeof(fpsText), "-- FPS");
    }
    _canvas.setTextColor(COLOR_GRAY);
    _canvas.setCursor(14, 18);
    _canvas.print(fpsText);

    // Bottom-Left: Camera Model & Battery level icon
    _canvas.setTextColor(COLOR_WHITE);
    _canvas.setCursor(14, _height - 24);
    if (model.length() > 0) {
        _canvas.print(model.substring(0, 8));
    } else {
        _canvas.print("RICOH GR");
    }
    drawBatteryIcon(14, _height - 12, battery.c_str());

    // Bottom-Right: RSSI (WiFi signal bars)
    drawWifiIcon(_width - 24, _height - 12, rssi);

    // Top-Right: Frame statistics
    char statsText[24];
    snprintf(statsText, sizeof(statsText), "%lu/%lu",
             static_cast<unsigned long>(frames),
             static_cast<unsigned long>(droppedFrames));
    _canvas.setTextColor(droppedFrames == 0 ? COLOR_WHITE : COLOR_YELLOW);
    _canvas.setCursor(_width - (strlen(statsText) * 6) - 14, 6);
    _canvas.print(statsText);
}

int16_t DisplayUi::width() const {
    return _width;
}

int16_t DisplayUi::height() const {
    return _height;
}

void DisplayUi::clear(uint16_t color) {
    _canvas.fillScreen(color);
}

void DisplayUi::drawStatusLines(const char* line1, const char* line2, const char* line3, const char* line4) {
    const char* s1 = safeText(line1);
    const char* s2 = safeText(line2);
    const char* s3 = safeText(line3);
    const char* s4 = safeText(line4);

    // Camera off / not awake yet: the user has not powered the camera on. When
    // off the camera either does not advertise, or (once it was on this session)
    // keeps advertising a standby beacon that the firmware must NOT chase. All of
    // these coalesce into one calm, actionable "turn on the camera" screen.
    const bool bleWaiting = statusContains(s1, s2, s3, s4, "CAMERA OFF") ||
                            statusContains(s1, s2, s3, s4, "TURN ON CAMERA") ||
                            statusContains(s1, s2, s3, s4, "CAMERA ASLEEP") ||
                            statusContains(s1, s2, s3, s4, "CAMERA STANDBY");

    // Boot / first-probe transient: the stick just powered on and is doing its
    // first passive awake scan before it knows whether the camera is on.
    const bool bleChecking = statusContains(s1, s2, s3, s4, "CHECKING CAMERA");

    // Genuinely online: the camera powered on and cleared the BLE power gate.
    // Bare BLE_READY does NOT count — during the camera-off standby poll the
    // firmware reaches BLE_READY every cycle, which must not read as connected.
    const bool bleOnline = statusContains(s1, s2, s3, s4, "WIFI CONNECTED") ||
                           statusContains(s1, s2, s3, s4, "HTTP PROBE OK") ||
                           statusContains(s1, s2, s3, s4, "HTTP") ||
                           statusContains(s1, s2, s3, s4, "LIVEVIEW") ||
                           statusContains(s1, s2, s3, s4, "LIVE VIEW") ||
                           statusContains(s1, s2, s3, s4, "BUTTON A SHUTTER") ||
                           statusContains(s1, s2, s3, s4, "SHOT OK");

    const bool bleConnected = bleOnline ||
                              statusContains(s1, s2, s3, s4, "BLE_READY") ||
                              statusContains(s1, s2, s3, s4, "BLE LINK READY");

    // Transient connect-phase markers. These sub-states pass through BLE_READY /
    // "BLE link ready" on the way up (checking power, opening Wi-Fi, recovery
    // retry) but are NOT the settled connected state -- without this demotion
    // they'd flash "CONNECTED" while we're still mid-connect (and, on a stale
    // standby link after camera-off, keep reading "CONNECTED" for ~30s). Genuine
    // online states (bleOnline) never carry these markers, so they stay
    // CONNECTED; only bare BLE_READY gets demoted to CONNECTING here.
    const bool bleConnecting = statusContains(s1, s2, s3, s4, "CHECKING POWER") ||
                               statusContains(s1, s2, s3, s4, "OPENING WIFI") ||
                               statusContains(s1, s2, s3, s4, "WIFI VIA BLE") ||
                               statusContains(s1, s2, s3, s4, "CAMERA RECOVERY") ||
                               statusContains(s1, s2, s3, s4, "CONNECTING");

    // No paired device yet: the firmware is scanning to pair (main passes
    // "Pairing GR BLE" / "Pairing mode").
    const bool blePairing = statusContains(s1, s2, s3, s4, "PAIRING");

    // Connect sub-step accent, derived from the raw status keywords, shown under
    // "CONNECTING..." so the user sees forward progress instead of a static word.
    const char* connectStep = "LINK";
    if (statusContains(s1, s2, s3, s4, "WIFI")) {
        connectStep = "WI-FI";
    } else if (statusContains(s1, s2, s3, s4, "HTTP") ||
               statusContains(s1, s2, s3, s4, "PROBE") ||
               statusContains(s1, s2, s3, s4, "LIVE")) {
        connectStep = "LIVE VIEW";
    }

    // Instantaneous classification. Everything not otherwise tagged — active
    // connect attempts and transient cooldown/retry churn — coalesces into one
    // calm "connecting" state so the user never sees the sub-second flip.
    enum BleUiStatus { Ui_Connecting, Ui_Connected, Ui_Waiting, Ui_Checking, Ui_Pairing };
    BleUiStatus raw = Ui_Connecting;
    if (bleConnected && !bleConnecting) {
        raw = Ui_Connected;
    } else if (bleWaiting) {
        raw = Ui_Waiting;
    } else if (blePairing) {
        raw = Ui_Pairing;
    } else if (bleChecking) {
        raw = Ui_Checking;
    }

    const uint32_t nowMs = millis();

    // NB: no camera-off latch. The firmware's camera-off wait state is
    // authoritative -- while it is active, main.cpp's updateStatusUiIfDue() forces
    // the "Camera off" status every tick, and the connect flow is blocked, so
    // there is no standby-reconnect churn to hide. When the wait state clears
    // (the user powered the camera on and a probe confirmed CAPTURE), the status
    // legitimately becomes "Connecting..." and MUST be shown -- an old latch that
    // held "CAMERA OFF" until full online is exactly what made a live reconnect
    // look stuck on "camera off". Classify purely on the current status.
    const BleUiStatus target = raw;

    // Minimum-dwell debounce: hold a status on screen for a floor before letting
    // it change, so a brief real state change can't flash. A change is applied
    // immediately when the previous status has already been shown longer than
    // the floor (the common case), so steady transitions aren't delayed.
    constexpr uint32_t kStatusMinDwellMs = 2000;
    static BleUiStatus displayed = Ui_Checking;
    static uint32_t lastChangeMs = 0;
    static bool dwellInit = false;
    // "Camera off" (Ui_Waiting) is the authoritative, settled wait state: the
    // sleep guard forces it every tick and blocks the connect flow, so it will
    // not flicker. Apply it IMMEDIATELY, bypassing the dwell floor -- otherwise a
    // brief "CONNECTING... / LINK" painted during the ~1.5s camera-off reconnect
    // attempt gets held for the full 2s floor and lingers on screen after the
    // flow has already concluded the camera is off (the "stuck on Connecting
    // Link" the user saw). All other transitions keep the anti-flash floor.
    if (!dwellInit) {
        displayed = target;
        lastChangeMs = nowMs;
        dwellInit = true;
    } else if (target != displayed &&
               (target == Ui_Waiting || (nowMs - lastChangeMs) >= kStatusMinDwellMs)) {
        displayed = target;
        lastChangeMs = nowMs;
    }

    // Full-bleed status screen: no card border or inset frame — the model name
    // and connection status sit directly on the background.
    const int16_t h = _height;

    // Collect the text lines for the current state, then render them as one
    // group that is vertically centered on the screen with an equal gap between
    // every line (the title included). Different states have different line
    // counts (2-4), so the block height is derived from the actual count rather
    // than a fixed 3-row assumption -- that keeps the group centered and the
    // spacing uniform in every state, instead of the block drifting up and the
    // last gap shrinking whenever an extra line (e.g. the pairing hint on the
    // camera-off screen) is present.
    constexpr int16_t kRowH = 16;   // textSize(2) glyph height
    constexpr int16_t kRowGap = 12;

    struct StatusRow {
        const char* text;
        uint16_t color;
    };
    StatusRow rows[4];
    uint8_t rowCount = 0;
    rows[rowCount++] = StatusRow{kModelLabel, COLOR_WHITE};

    if (displayed == Ui_Waiting) {
        rows[rowCount++] = StatusRow{"CAMERA OFF", COLOR_WHITE};
        rows[rowCount++] = StatusRow{"TURN ON TO CONNECT", COLOR_AMBER};
        // Either button (KEY2 or the front BtnA) held 3s clears BLE pairing and
        // starts a fresh pairing scan -- surface that on the screen the user is
        // looking at when they'd want to re-pair.
        rows[rowCount++] = StatusRow{"HOLD 3S TO PAIR", COLOR_AMBER};
    } else if (displayed == Ui_Pairing) {
        // Two lines because "ENABLE PAIRING ON CAMERA" at size 2 exceeds 240px.
        rows[rowCount++] = StatusRow{"ENABLE PAIRING", COLOR_AMBER};
        rows[rowCount++] = StatusRow{"ON CAMERA", COLOR_AMBER};
    } else {
        const char* bleStatus = (displayed == Ui_Connected) ? "CONNECTED"
                              : (displayed == Ui_Checking)  ? "CHECKING..."
                                                            : "CONNECTING...";
        rows[rowCount++] = StatusRow{bleStatus, COLOR_WHITE};
        // Show the live connect sub-step under "CONNECTING..." so the multi-
        // second link/Wi-Fi/live-view bring-up reads as forward progress.
        if (displayed == Ui_Connecting) {
            rows[rowCount++] = StatusRow{connectStep, COLOR_GRAY};
        }
    }

    const int16_t blockH =
        static_cast<int16_t>(rowCount * kRowH + (rowCount - 1) * kRowGap);
    int16_t rowY = static_cast<int16_t>((h - blockH) / 2);
    _canvas.setTextSize(2);
    for (uint8_t i = 0; i < rowCount; ++i) {
        const int16_t w = static_cast<int16_t>(strlen(rows[i].text) * 12);
        _canvas.setTextColor(rows[i].color, COLOR_BG);
        _canvas.setCursor((_width - w) / 2, rowY);
        _canvas.print(rows[i].text);
        rowY = static_cast<int16_t>(rowY + kRowH + kRowGap);
    }
}
// Graphic helper to draw WiFi RSSI strength bars
void DisplayUi::drawWifiIcon(int16_t x, int16_t y, int32_t rssi) {
    uint8_t bars = 0;
    if (rssi < 0) {
        if (rssi >= -60) bars = 4;
        else if (rssi >= -70) bars = 3;
        else if (rssi >= -80) bars = 2;
        else bars = 1;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t color = (i < bars) ? COLOR_GREEN : COLOR_SLATE;
        int16_t barH = 2 + (i * 2);
        _canvas.fillRect(x + (i * 3), y + (8 - barH), 2, barH, color);
    }
}

// Graphic helper to draw dynamic battery outline & fill level
void DisplayUi::drawBatteryIcon(int16_t x, int16_t y, const char* batteryStr) {
    int pct = -1;
    const char* text = safeText(batteryStr);
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (isDigit(text[i])) {
            if (pct < 0) pct = 0;
            pct = pct * 10 + (text[i] - '0');
        } else if (text[i] == '%' || text[i] == ' ') {
            break;
        }
    }

    _canvas.drawRect(x, y, 14, 8, COLOR_WHITE);
    _canvas.fillRect(x + 14, y + 2, 1, 4, COLOR_WHITE);

    if (pct >= 0) {
        if (pct > 100) pct = 100;
        int fillW = (pct * 10) / 100;
        if (fillW > 10) fillW = 10;

        uint16_t color = COLOR_GREEN;
        if (pct < 20) color = COLOR_RED;
        else if (pct < 50) color = COLOR_YELLOW;

        _canvas.fillRect(x + 2, y + 2, fillW, 4, color);
    } else {
        _canvas.drawLine(x + 2, y + 2, x + 11, y + 5, COLOR_GRAY);
    }
}
