#include <Arduino.h>
#include <esp_heap_caps.h>

#include "buttons.h"
#include "camera_profile_store.h"
#include "config.h"
#include "display.h"
#include "g11_button.h"
#include "gr_api.h"
#include "gr_shutter_service.h"
#include "gr_wifi.h"
#include "jpeg_decoder.h"
#include "mjpeg_stream.h"
#include "ricoh_ble_client.h"

namespace {

GrWifi grWifi;
GrApi grApi;
MjpegStream mjpeg;
DisplayUi ui;
Buttons buttons;
G11Button g11Button;
JpegDecoder decoder;
CameraProfileStore profileStore;
CameraProfile cameraProfile;
RicohBleClient ricohBle;
GrShutterService shutterService;

uint8_t* frameBuffer = nullptr;
uint8_t streamReadBuffer[STREAM_READ_BUFFER_SIZE];

CameraProps cameraProps;

bool liveviewEnabled = true;
uint32_t lastFrameAt = 0;
uint32_t lastStatusAt = 0;
uint32_t lastPropsAt = 0;
uint32_t decodedFrames = 0;
uint32_t fpsWindowStart = 0;
uint32_t fpsWindowFrames = 0;
float currentFps = 0.0f;
uint32_t lastOverlayAt = 0;
uint32_t lastStatusDrawAt = 0;
String lastStatusLine1;
String lastStatusLine2;
String lastStatusLine3;
String lastStatusLine4;

constexpr uint32_t STATUS_MIN_REDRAW_MS = 1500;
constexpr uint32_t LIVE_OVERLAY_INTERVAL_MS = 1000;
constexpr bool DRAW_LIVE_OVERLAY = false;  // Pure viewfinder mode: avoid top/bottom bar flicker.

void showStatusIfChanged(const String& line1,
                         const String& line2 = String(),
                         const String& line3 = String(),
                         const String& line4 = String(),
                         bool force = false) {
  const uint32_t now = millis();
  const bool changed = force ||
                       line1 != lastStatusLine1 ||
                       line2 != lastStatusLine2 ||
                       line3 != lastStatusLine3 ||
                       line4 != lastStatusLine4;
  if (!changed) {
    return;
  }
  if (!force && (now - lastStatusDrawAt) < STATUS_MIN_REDRAW_MS) {
    return;
  }

  ui.showStatus(line1, line2, line3, line4);
  lastStatusLine1 = line1;
  lastStatusLine2 = line2;
  lastStatusLine3 = line3;
  lastStatusLine4 = line4;
  lastStatusDrawAt = now;
}

void closeLiveView(const char* reason) {
  if (grApi.isLiveViewOpen()) {
    Serial.printf("LiveView: closing (%s)\n", reason);
  }
  grApi.closeLiveView();
  mjpeg.reset();
}

void applyDefaultProfile() {
  if (!profileStore.load(cameraProfile)) {
    Serial.println("Profile: NVS open failed, using build defaults");
    cameraProfile = CameraProfile{};
  }

  if (cameraProfile.wifi.ssid.isEmpty()) {
    cameraProfile.wifi.ssid = GR_WIFI_SSID;
  }
  if (cameraProfile.wifi.passphrase.isEmpty()) {
    cameraProfile.wifi.passphrase = GR_WIFI_PASSWORD;
  }
  if (cameraProfile.wifi.cameraIp.isEmpty()) {
    cameraProfile.wifi.cameraIp = GR_HOST;
  }
  if (cameraProfile.shutter.timeoutMs == 0) {
    cameraProfile.shutter.timeoutMs = SHUTTER_TIMEOUT_MS;
  }
  grApi.setEndpoint(cameraProfile.wifi.cameraIp.c_str(), GR_PORT);

  Serial.printf("Profile: camera='%s' ble='%s' wifi='%s' ip='%s' shutter=%s\n",
                cameraProfile.cameraName.c_str(),
                cameraProfile.bleAddress.c_str(),
                cameraProfile.wifi.ssid.c_str(),
                cameraProfile.wifi.cameraIp.c_str(),
                cameraProfile.shutter.isValid() ? "configured" : "not-set");
}

void runBleDiscoveryAtBoot() {
  showStatusIfChanged("Scanning GR BLE", cameraProfile.bleAddress, cameraProfile.wifi.ssid, "", true);
  RicohBleDeviceInfo info = ricohBle.scanForCamera(cameraProfile.bleAddress, BLE_SCAN_SECONDS);
  if (!info.found) {
    showStatusIfChanged("BLE not found", "Continue WiFi", cameraProfile.wifi.ssid, "", true);
    return;
  }

  cameraProfile.cameraName = info.name;
  cameraProfile.bleAddress = info.address;
  profileStore.saveBleIdentity(cameraProfile.cameraName, cameraProfile.bleAddress);

  showStatusIfChanged("BLE camera found", info.name, info.address, "Discovering...", true);
  if (ricohBle.connectAndDiscover(info, BLE_CONNECT_TIMEOUT_MS)) {
    Serial.println("BLE Provision: Wi-Fi provisioning skeleton ready; secure characteristic map not configured yet");
    showStatusIfChanged("BLE ready", info.name, info.address, "WiFi next", true);
  } else {
    showStatusIfChanged("BLE discover failed", ricohBle.lastError(), "WiFi next", "", true);
  }
  ricohBle.disconnect();
}

bool connectWifiFromProfile(bool forceStatus) {
  grApi.setEndpoint(cameraProfile.wifi.cameraIp.c_str(), GR_PORT);
  showStatusIfChanged("Connecting WiFi", cameraProfile.wifi.ssid, cameraProfile.wifi.cameraIp, "", forceStatus);
  Serial.printf("WiFi: connecting to '%s' host=%s\n",
                cameraProfile.wifi.ssid.c_str(),
                cameraProfile.wifi.cameraIp.c_str());

  if (grWifi.connect(cameraProfile.wifi.ssid.c_str(), cameraProfile.wifi.passphrase.c_str(), WIFI_CONNECT_TIMEOUT_MS)) {
    showStatusIfChanged("WiFi connected", grWifi.localIPString(), cameraProfile.wifi.cameraIp, "", true);
    Serial.printf("WiFi: connected ip=%s rssi=%ld\n",
                  grWifi.localIPString().c_str(),
                  static_cast<long>(grWifi.rssi()));
    return true;
  }

  showStatusIfChanged("WiFi pending", grWifi.statusText(), cameraProfile.wifi.ssid, "", true);
  Serial.printf("WiFi: connect failed status=%s\n", grWifi.statusText().c_str());
  return false;
}

void refreshPropsIfDue(bool force = false) {
  const uint32_t now = millis();
  if (!force && (now - lastPropsAt) < PROPS_REFRESH_INTERVAL_MS) {
    return;
  }
  if (!grWifi.isConnected()) {
    return;
  }

  if (force) {
    String statusBody;
    if (grApi.fetchStatusDevice(statusBody, PROPS_TIMEOUT_MS)) {
      Serial.printf("StatusDevice: %u bytes\n", static_cast<unsigned>(statusBody.length()));
    } else {
      Serial.printf("StatusDevice: failed: %s\n", grApi.lastError().c_str());
    }
  }

  CameraProps nextProps;
  if (grApi.fetchProps(nextProps, PROPS_TIMEOUT_MS)) {
    cameraProps = nextProps;
    lastPropsAt = now;
    Serial.printf("Props: model='%s' battery='%s'\n",
                  cameraProps.model.c_str(),
                  cameraProps.battery.c_str());
  } else {
    Serial.printf("Props: failed: %s\n", grApi.lastError().c_str());
  }
}

void updateFps() {
  const uint32_t now = millis();
  fpsWindowFrames++;
  if (fpsWindowStart == 0) {
    fpsWindowStart = now;
  }
  const uint32_t elapsed = now - fpsWindowStart;
  if (elapsed >= 1000) {
    currentFps = (fpsWindowFrames * 1000.0f) / static_cast<float>(elapsed);
    fpsWindowFrames = 0;
    fpsWindowStart = now;
  }
}

void onJpegFrame(const uint8_t* data, size_t len, void*) {
  lastFrameAt = millis();
  decodedFrames++;
  updateFps();

  if (!decoder.drawFrame(data, len, currentFps)) {
    Serial.printf("JPEG frame %u bytes decode failed: %s\n",
                  static_cast<unsigned>(len),
                  decoder.lastError().c_str());
  } else {
    Serial.printf("JPEG frame: %u bytes, %dx%d, decode=%lums\n",
                  static_cast<unsigned>(len),
                  decoder.lastWidth(),
                  decoder.lastHeight(),
                  static_cast<unsigned long>(decoder.lastDecodeMs()));
  }
  lastFrameAt = millis();

  const uint32_t now = millis();
  if (DRAW_LIVE_OVERLAY && (now - lastOverlayAt) >= LIVE_OVERLAY_INTERVAL_MS) {
    lastOverlayAt = now;
    ui.drawOverlay(grWifi.statusText(),
                   grApi.isLiveViewOpen() ? "LIVE" : "IDLE",
                   cameraProps.model,
                   cameraProps.battery,
                   currentFps,
                   grWifi.rssi(),
                   decodedFrames,
                   mjpeg.droppedFrames());
  }
}

void ensureWiFi() {
  grWifi.loop();
  if (!grWifi.isConnected()) {
    closeLiveView("wifi disconnected");
  }
}

void ensureLiveView() {
  if (!liveviewEnabled || !grWifi.isConnected()) {
    return;
  }

  uint32_t now = millis();
  if (!grApi.isLiveViewOpen()) {
    showStatusIfChanged("Opening liveview", grWifi.localIPString(), cameraProps.model, cameraProps.battery);
    if (grApi.openLiveView()) {
      lastFrameAt = now;
      mjpeg.reset();
      Serial.println("LiveView: connected");
    } else {
      Serial.printf("LiveView: open failed: %s\n", grApi.lastError().c_str());
      delay(250);
    }
    return;
  }

  const int readLen = grApi.readLiveView(streamReadBuffer, sizeof(streamReadBuffer));
  if (readLen > 0) {
    mjpeg.process(streamReadBuffer, static_cast<size_t>(readLen));
  } else if (readLen < 0) {
    Serial.printf("LiveView: read failed: %s\n", grApi.lastError().c_str());
    closeLiveView("read failed");
    return;
  }

  now = millis();
  if ((now - lastFrameAt) > LIVEVIEW_STALL_TIMEOUT_MS) {
    Serial.println("LiveView: stalled, reconnecting");
    closeLiveView("stall watchdog");
    lastFrameAt = now;
  }
}

void attemptReconnect(const char* reason) {
  Serial.printf("Reconnect: %s\n", reason);
  closeLiveView(reason);
  liveviewEnabled = true;
  grWifi.disconnect();
  delay(100);
  connectWifiFromProfile(true);
  refreshPropsIfDue(true);
  lastFrameAt = millis();
}

void triggerShutterFromG11() {
  Serial.println("G11: short press shutter trigger");
  if (!grWifi.isConnected()) {
    showStatusIfChanged("G11 shutter", "WiFi not connected", grWifi.statusText(), "", true);
    return;
  }

  if (!cameraProfile.shutter.isValid()) {
    showStatusIfChanged("G11 shutter", "API NOT SET", "Configure endpoint", "", true);
    Serial.println("Shutter: API NOT SET; no request sent");
    return;
  }

  if (cameraProfile.shutter.closeLiveviewBeforeShoot) {
    closeLiveView("shutter preflight");
  }

  showStatusIfChanged("G11 shutter", "Sending...", cameraProfile.shutter.path, "", true);
  String message;
  if (shutterService.shoot(grApi, cameraProfile.shutter, message)) {
    showStatusIfChanged("G11 shutter", message, cameraProps.model, cameraProps.battery, true);
    Serial.printf("Shutter: %s\n", message.c_str());
  } else {
    showStatusIfChanged("G11 shutter failed", message, cameraProfile.shutter.path, "", true);
    Serial.printf("Shutter: failed: %s\n", message.c_str());
  }

  if (cameraProfile.shutter.closeLiveviewBeforeShoot && liveviewEnabled) {
    closeLiveView("shutter resume");
    lastFrameAt = millis();
  }
}

void handleButtons() {
  const ButtonEvents events = buttons.poll();
  if (events.buttonA) {
    Serial.println("Button A reserved: shutter endpoint is not verified; no request sent.");
    showStatusIfChanged("A reserved", "Use G11 for V2", "API must be configured", "", true);
  }

  if (events.buttonB) {
    liveviewEnabled = !liveviewEnabled;
    if (!liveviewEnabled) {
      closeLiveView("button B pause");
      showStatusIfChanged("Liveview paused", "Press B to resume", cameraProps.model, cameraProps.battery, true);
    } else {
      closeLiveView("button B reconnect");
      showStatusIfChanged("Liveview enabled", "Reconnecting...", cameraProps.model, cameraProps.battery, true);
    }
  }

  const G11ButtonEvents g11 = g11Button.poll();
  if (g11.longPress) {
    showStatusIfChanged("G11 long press", "Reconnect WiFi/liveview", cameraProfile.wifi.ssid, "", true);
    attemptReconnect("G11 long press");
  } else if (g11.shortPress) {
    triggerShutterFromG11();
  }
}

void updateStatusUiIfDue() {
  const uint32_t now = millis();
  if ((now - lastStatusAt) < UI_STATUS_INTERVAL_MS) {
    return;
  }
  lastStatusAt = now;

  if (!grApi.isLiveViewOpen()) {
    showStatusIfChanged(grWifi.statusText(),
                        liveviewEnabled ? grWifi.localIPString() : "Preview paused",
                        cameraProps.model,
                        cameraProps.battery);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("RICOH GR StickS3 Remote Viewfinder V2");

  ui.begin();
  buttons.begin();
  g11Button.begin();
  decoder.begin();
  ui.showBoot("V2 booting...");

  applyDefaultProfile();
  runBleDiscoveryAtBoot();

  if (!psramFound()) {
    Serial.println("PSRAM not found; JPEG buffer allocation will likely fail.");
    ui.showError("PSRAM not found");
  }

  frameBuffer = static_cast<uint8_t*>(
      heap_caps_malloc(FRAME_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (frameBuffer == nullptr) {
    frameBuffer = static_cast<uint8_t*>(heap_caps_malloc(FRAME_BUFFER_SIZE, MALLOC_CAP_8BIT));
  }
  if (frameBuffer == nullptr) {
    Serial.println("Failed to allocate JPEG frame buffer.");
    ui.showError("Frame buffer alloc failed");
    while (true) {
      delay(1000);
    }
  }

  mjpeg.begin(frameBuffer, FRAME_BUFFER_SIZE, onJpegFrame, nullptr);

  grWifi.begin();
  if (connectWifiFromProfile(true)) {
    refreshPropsIfDue(true);
  }

  lastFrameAt = millis();
  lastStatusAt = 0;
}

void loop() {
  handleButtons();
  ensureWiFi();
  refreshPropsIfDue();
  ensureLiveView();
  updateStatusUiIfDue();
  delay(1);
}