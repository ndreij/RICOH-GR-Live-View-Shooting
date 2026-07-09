#include <Arduino.h>
#include <M5PM1.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "buttons.h"
#include "camera_identity.h"
#include "camera_profile_store.h"
#include "config.h"
#include "app/AppController.h"
#include "core/AppConfig.h"
#include "core/Logger.h"
#include "display.h"
#include "gr_api.h"
#include "gr_wifi.h"
#include "jpeg_decoder.h"
#include "mjpeg_stream.h"
#include "ricoh_ble_client.h"
#include "services/BleCameraService.h"
#include "services/CameraPowerPolicy.h"
#include "services/PreviewFrameBuffer.h"
#include "services/WifiPreviewService.h"
#include "supervisor/SystemSupervisor.h"
#include "ui/ButtonInput.h"

namespace {

GrWifi grWifi;
GrApi grApi;
MjpegStream mjpeg;
DisplayUi ui;
Buttons buttons;
JpegDecoder decoder;
CameraProfileStore profileStore;
CameraProfile cameraProfile;
RicohBleClient ricohBle;
rvf::BleCameraService bleCamera(ricohBle);
M5PM1 stickPower;
rvf::CameraPowerPolicy cameraPowerPolicy;
rvf::WifiPreviewService wifiPreview(grWifi, grApi, mjpeg);
rvf::PreviewFrameBuffer previewFrameBuffer;
rvf::SystemSupervisor systemSupervisor;
using CameraFlowState = rvf::AppState;
rvf::AppController appController(CameraFlowState::BleScan);
uint8_t* frameBuffer = nullptr;
uint8_t streamReadBuffer[rvf::AppConfig::Buffer::kStreamReadBufferSize];
CameraProps cameraProps;
CameraProps pendingCameraProps;
RicohBleWifiCredentials pendingFreshWifiCredentials;

bool liveviewEnabled = true;
uint32_t lastFrameAt = 0;
uint32_t lastLiveViewActivityAt = 0;
uint32_t lastStatusAt = 0;
uint32_t powerButtonLastPollAt = 0;
uint32_t powerButtonPressedSince = 0;
bool powerButtonHoldReported = false;
bool powerButtonPrevPressed = false;
uint32_t powerButtonLastClickAt = 0;
bool stickPowerReady = false;

// Forward declaration: the BLE busy-loop callback (serviceButtonsDuringBleOperation,
// defined well before shutdownStickS3) needs to trigger a graceful power-off when
// the user double-clicks PWR during a blocking scan.
void shutdownStickS3();

// Two press edges within this window = double-click -> power off. The M5PM1 does
// not surface a usable DOUBLE-click IRQ (it flags plain button activity as WAKE),
// so firmware detects the gesture itself from the raw pressed/released state.
constexpr uint32_t POWER_BUTTON_DOUBLE_CLICK_MS = 700;
uint32_t lastCameraRecoveryAt = 0;
bool cameraRecoveryInProgress = false;
bool setupCameraFlowActive = false;
// Bounded-recovery fallback for camera power-off events that never report a
// clean remote disconnect reason (see CAMERA_OFF_RECOVERY_FALLBACK_* in
// config.h). cameraRecoveryFirstFailureAt = millis() of the first failed
// recovery in the current run (0 = none pending); cameraRecoveryFailureCount =
// consecutive failed recoveries since. Both reset on any successful recovery.
uint32_t cameraRecoveryFirstFailureAt = 0;
uint16_t cameraRecoveryFailureCount = 0;

// Preview orientation, driven by the StickS3 IMU. When the device is rotated
// 180 degrees in the plane of the screen we flip the live image so it stays
// upright. Disabled automatically if no IMU is detected.
bool imuAvailable = false;
bool previewFlipped = false;
uint32_t lastOrientationPollAt = 0;
// Count of consecutive slow/failed accel reads. When it reaches
// IMU_MAX_READ_PROBLEMS we stop polling (imuAvailable=false) so a contended I2C
// bus never keeps starving LiveView. Reset to 0 on any fast, successful read.
uint8_t orientationReadProblems = 0;

// Timestamp of the first of a run of consecutive LiveView read errors (0 = none
// pending). Used to ride out the brief stream interruption while the camera
// captures a photo without tearing down the preview. See
// readAndProcessLiveViewFrameForController.
uint32_t liveViewReadErrorSince = 0;
// While a photo is being captured the camera blanks its LiveView feed (it emits
// near-black exposure frames and briefly pauses the MJPEG stream). Freezing the
// last good frame on screen until this deadline hides that flash. 0 = no freeze.
uint32_t previewFreezeUntil = 0;
uint32_t previewFreezeSkipped = 0;
bool key2PairingResetRequested = false;
bool cameraAutoWakeBlocked = false;
int cameraAutoWakeDisconnectReason = 0;
// True once the passive awake scan has run at least once. Drives the idle status
// text: before the first scan completes we don't yet know whether the camera is
// on, so we show a transient "Checking camera" instead of the settled
// "Camera off / Turn on to connect".
bool firstAwakeScanDone = false;
// When set, the next runBleDiscoveryAtBoot skips the passive advert power-bit
// gate (see below). A user-initiated manual wake (BtnA) and the auto-probe
// fast reconnect have already confirmed the camera is awake, so they must not
// be blocked by a standby-beacon reading. Consumed (reset to false) by the
// gate on entry.
bool bleSkipAwakeGateOnce = false;
uint32_t cameraPowerProbeBackoffUntil = 0;
RicohCameraPowerState cameraPowerState = RicohCameraPowerState::Unknown;
RicohCameraOperationMode cameraOperationMode = RicohCameraOperationMode::Unknown;
uint32_t lastPropsAt = 0;
uint32_t decodedFrames = 0;
uint32_t fpsWindowStart = 0;
uint32_t fpsWindowFrames = 0;
float currentFps = 0.0f;
uint32_t lastStatusDrawAt = 0;
String lastStatusLine1;
String lastStatusLine2;
String lastStatusLine3;
String lastStatusLine4;
bool wifiCacheRefreshPending = false;
uint32_t wifiCacheRefreshAfter = 0;

constexpr uint32_t STATUS_MIN_REDRAW_MS = 1500;
// LiveView is shown full-screen with no HUD overlay: the crop marks / AF bracket
// / FPS / model / battery text were too small to read on the 240x135 LCD and
// only cluttered the preview. Set to true to bring the diagnostic HUD back.
constexpr bool DRAW_LIVE_OVERLAY = false;

void requestManualCameraWake(const char* source, bool fastReconnect = false);
void resetBlePairingFromKey2();
rvf::AppFlowActions makeAppFlowActions();
void updatePreviewOrientation();

bool beginStickPower() {
  // Route the M5PM1 power chip through M5Unified's coordinated internal I2C bus
  // (M5.In_I2C) rather than a second raw Wire master on the same physical pins.
  // The IMU (M5.Imu) already uses M5.In_I2C; two independent masters on one bus
  // collide (ESP_ERR_TIMEOUT / "Wire Error 263" floods, each failed read blocks
  // the main loop and starves the LiveView socket -> fps collapse). Sharing the
  // single I2C_Class serializes all internal-bus access. The M5PM1 lib documents
  // exactly this fix ("M5Unified owns its I2C_Class bus. Fix: use
  // pm1.begin(&M5.In_I2C, addr, freq)"). M5.begin() already begin()-ed In_I2C.
  const m5pm1_err_t err = stickPower.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
  stickPowerReady = (err == M5PM1_OK);
  if (!stickPowerReady) {
    LOGW("POWER", "Power: M5PM1 begin failed err=%d; fallback to M5Unified power API", static_cast<int>(err));
    return false;
  }

  // Disable the M5PM1 *hardware* double-click power-off. By default a double-click
  // makes the chip cut power instantly, bypassing firmware entirely -> our graceful
  // shutdownStickS3() (which sends the BLE camera power-off) never runs. With it
  // disabled, clicks no longer trigger any hardware action, so firmware is free to
  // detect the double-click gesture itself (see pollStickPowerHold) and run a
  // graceful shutdown.
  stickPower.setDoubleOffDisable(true);
  // Push the M5PM1 hardware long-press power-off delay to its maximum (4s). The
  // chip default is 1s, which fires BEFORE firmware detects the hold at
  // POWER_BUTTON_HOLD_MS (1200ms) -> the hardware cuts power and our graceful
  // shutdownStickS3() never runs. With a 4s hardware timeout, firmware wins the
  // race: it detects the hold at 1200ms, commands the camera off over BLE, tears
  // down transports, then issues the M5PM1 shutdown itself — well inside the 4s
  // hardware safety window (which remains as a last-resort force-off).
  stickPower.btnSetConfig(M5PM1_BTN_TYPE_LONG, M5PM1_BTN_LONG_PRESS_DELAY_4000MS);

  bool pressed = false;
  stickPower.btnGetState(&pressed);
  LOGLINE_I("POWER", "Power: M5PM1 ready");
  return true;
}

bool isStickPowerButtonPressed() {
  if (stickPowerReady) {
    bool pressed = false;
    if (stickPower.btnGetState(&pressed) == M5PM1_OK) {
      return pressed;
    }
  }
  return M5.BtnPWR.isPressed();
}

bool pollStickPowerHold() {
  const uint32_t now = millis();
  if ((now - powerButtonLastPollAt) < POWER_BUTTON_POLL_MS) {
    return false;
  }
  powerButtonLastPollAt = now;

  const bool pressed = isStickPowerButtonPressed();

  // Double-click power-off: the hardware double-off action is disabled (see
  // beginStickPower) so the chip no longer cuts power on a double-click, and it
  // doesn't surface a usable DOUBLE-click IRQ (button taps read back as WAKE).
  // Detect the gesture in firmware: two rising edges (press starts) within
  // POWER_BUTTON_DOUBLE_CLICK_MS -> graceful shutdown (camera off over BLE first).
  if (pressed && !powerButtonPrevPressed) {
    if (powerButtonLastClickAt != 0 &&
        (now - powerButtonLastClickAt) <= POWER_BUTTON_DOUBLE_CLICK_MS) {
      powerButtonLastClickAt = 0;
      powerButtonPrevPressed = pressed;
      Serial.println("Power: double-click power-off detected");
      return true;
    }
    powerButtonLastClickAt = now;
  }
  powerButtonPrevPressed = pressed;

  if (!pressed) {
    powerButtonPressedSince = 0;
    powerButtonHoldReported = false;
    return false;
  }

  if (powerButtonPressedSince == 0) {
    powerButtonPressedSince = now;
  }
  if (!powerButtonHoldReported && (now - powerButtonPressedSince) >= POWER_BUTTON_HOLD_MS) {
    powerButtonHoldReported = true;
    return true;
  }
  return false;
}

const char* cameraPowerStateName(RicohCameraPowerState state) {
  switch (state) {
    case RicohCameraPowerState::On:
      return "ON";
    case RicohCameraPowerState::OffOrShuttingDown:
      return "OFF";
    case RicohCameraPowerState::Unknown:
      return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* cameraOperationModeName(RicohCameraOperationMode mode) {
  switch (mode) {
    case RicohCameraOperationMode::Capture:
      return "CAPTURE";
    case RicohCameraOperationMode::Playback:
      return "PLAYBACK";
    case RicohCameraOperationMode::BleStartup:
      return "BLE_STARTUP";
    case RicohCameraOperationMode::Other:
      return "OTHER";
    case RicohCameraOperationMode::PowerOffTransfer:
      return "POWER_OFF_TRANSFER";
    case RicohCameraOperationMode::Unknown:
      return "UNKNOWN";
  }
  return "UNKNOWN";
}

bool isCameraStandbyOperationMode(RicohCameraOperationMode mode) {
  // Asleep/standby modes that must block Wi-Fi and NOT be woken via WLAN-ON.
  // POWER_OFF_TRANSFER is always standby. BLE_STARTUP is standby when
  // RICOH_BLE_STARTUP_IS_STANDBY is set: verified on a real GR IIIx (2026-07),
  // a link opened while the camera is asleep latches BLE_STARTUP for its entire
  // lifetime (re-reads and Notify never update), so we must NOT drive WLAN-ON to
  // wake it (extends the lens — unsafe). Instead the flow holds, sends no
  // WLAN-ON, disconnects, and re-samples the true mode on periodic FRESH
  // reconnects; a fresh connect made after the user powers the camera on reads
  // CAPTURE and flows through to Wi-Fi/LiveView. See RICOH_BLE_STARTUP_IS_STANDBY
  // in config.h for the full rationale.
  return mode == RicohCameraOperationMode::PowerOffTransfer ||
         (RICOH_BLE_STARTUP_IS_STANDBY && mode == RicohCameraOperationMode::BleStartup);
}

rvf::CameraPowerStatus toPolicyPowerStatus(RicohCameraPowerState state) {
  switch (state) {
    case RicohCameraPowerState::On:
      return rvf::CameraPowerStatus::On;
    case RicohCameraPowerState::OffOrShuttingDown:
      return rvf::CameraPowerStatus::Off;
    case RicohCameraPowerState::Unknown:
      return rvf::CameraPowerStatus::Unknown;
  }
  return rvf::CameraPowerStatus::Unknown;
}

rvf::CameraOperationStatus toPolicyOperationStatus(RicohCameraOperationMode mode) {
  if (isCameraStandbyOperationMode(mode)) {
    return rvf::CameraOperationStatus::Standby;
  }
  if (mode == RicohCameraOperationMode::Unknown) {
    return rvf::CameraOperationStatus::Unknown;
  }
  return rvf::CameraOperationStatus::Ready;
}

void setCameraFlowState(CameraFlowState state, const char* reason) {
  appController.transitionTo(state, reason, millis());
}

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

void waitForSerialConsole() {
  const uint32_t startMs = millis();
  while (!Serial && (millis() - startMs) < rvf::AppConfig::SerialPort::kBootWaitMs) {
    delay(10);
  }
  Serial.setDebugOutput(false);
  Serial.println();
  LOGI("BOOT", "%s", rvf::AppConfig::Ui::kBanner);
}

void closeLiveView(const char* reason) {
  if (wifiPreview.isPreviewRunning()) {
    Serial.printf("LiveView: closing (%s)\n", reason != nullptr ? reason : "reset");
  }
  previewFreezeUntil = 0;
  wifiPreview.stopPreview();
}

String preferredBleName() {
  if (cameraProfile.cameraName.length() > 0) {
    return cameraProfile.cameraName;
  }
  const std::string derivedName = deriveBleNameFromWifiSsid(cameraProfile.wifi.ssid.c_str());
  return String(derivedName.c_str());
}

bool timeReached(uint32_t deadlineMs) {
  return static_cast<int32_t>(millis() - deadlineMs) >= 0;
}

bool isCameraPowerOffDisconnectReason(int reason) {
  return reason == RICOH_BLE_DISCONNECT_REMOTE_USER ||
         reason == RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF;
}

bool cameraSleepGuardActive() {
  // Camera-off wait mode is user-triggered: while it is active the controller
  // must NOT auto-scan/reconnect (that churn keeps a GR IIIx stuck in
  // BLE_STARTUP; see config.h). The stick idles until the user powers the
  // camera on and presses BtnA, which routes to requestManualCameraWake().
  return cameraAutoWakeBlocked;
}

bool cameraPowerProbeBackoffActive() {
  return cameraAutoWakeBlocked &&
         cameraPowerProbeBackoffUntil != 0 &&
         !timeReached(cameraPowerProbeBackoffUntil);
}

uint32_t cameraPowerProbeBackoffRemainingMs() {
  if (!cameraPowerProbeBackoffActive()) {
    return 0;
  }
  return cameraPowerProbeBackoffUntil - millis();
}

void scheduleCameraPowerProbeBackoff(const char* source) {
  cameraPowerProbeBackoffUntil = millis() + CAMERA_POWER_OFF_PROBE_BACKOFF_MS;
  Serial.printf("BLE guard: next power probe in %lums (%s)\n",
                static_cast<unsigned long>(CAMERA_POWER_OFF_PROBE_BACKOFF_MS),
                source != nullptr ? source : "camera standby");
}

void showCameraSleepGuardStatus(bool force = false) {
  // Until the first passive awake scan finishes we can't tell whether the camera
  // is off or just not seen yet, so show the boot/first-probe transient. After
  // that, settle on the actionable "Camera off / Turn on to connect" screen. The
  // first status word ("Checking camera" / "Camera off") is what display.cpp
  // classifies on -- keep these keywords in sync with drawStatusLines().
  if (!firstAwakeScanDone) {
    showStatusIfChanged("Checking camera", "Please wait...", preferredBleName(), "", force);
    return;
  }
  showStatusIfChanged("Camera off", "Turn on to connect", preferredBleName(), "", force);
}

void enterCameraSleepGuard(const char* source, int reason) {
  const char* guardSource = source != nullptr ? source : "camera standby";
  // Entering the guard means we already know the camera is off/shutting down, so
  // skip the "Checking camera" transient and settle straight on "Camera off".
  firstAwakeScanDone = true;
  // The guard supersedes any in-flight bounded-recovery fallback tracking.
  cameraRecoveryFirstFailureAt = 0;
  cameraRecoveryFailureCount = 0;
  if (cameraAutoWakeBlocked) {
    if (reason != 0 && cameraAutoWakeDisconnectReason == 0) {
      cameraAutoWakeDisconnectReason = reason;
    }
    cameraPowerState = RicohCameraPowerState::OffOrShuttingDown;
    cameraOperationMode = RicohCameraOperationMode::Unknown;
    scheduleCameraPowerProbeBackoff(guardSource);
    closeLiveView(guardSource);
    wifiPreview.disconnectWifi();
    bleCamera.disconnect();
    setCameraFlowState(CameraFlowState::CameraPowerOff, guardSource);
    showCameraSleepGuardStatus(false);
    lastFrameAt = millis();
    lastCameraRecoveryAt = millis();
    return;
  }

  cameraPowerState = RicohCameraPowerState::OffOrShuttingDown;
  cameraOperationMode = RicohCameraOperationMode::Unknown;
  cameraAutoWakeBlocked = true;
  cameraAutoWakeDisconnectReason = reason;
  scheduleCameraPowerProbeBackoff(guardSource);

  closeLiveView(guardSource);
  wifiPreview.disconnectWifi();
  bleCamera.disconnect();
  setCameraFlowState(CameraFlowState::CameraPowerOff, guardSource);
  Serial.printf("BLE guard: remote disconnect reason=%d; idle until user powers camera on + presses BtnA\n",
                reason);
  showCameraSleepGuardStatus(true);
  lastFrameAt = millis();
  lastCameraRecoveryAt = millis();
}

bool consumeCameraPowerOffNotification(const char* source) {
  if (!bleCamera.consumePowerOffNotification()) {
    return false;
  }

  enterCameraSleepGuard(source != nullptr ? source : "BLE power notify 0x00", RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF);
  return true;
}

bool settleAndConsumeCameraPowerOffNotification(const char* source) {
  if (RICOH_BLE_POWER_NOTIFY_SETTLE_MS > 0) {
    delay(RICOH_BLE_POWER_NOTIFY_SETTLE_MS);
    yield();
  }
  return consumeCameraPowerOffNotification(source);
}

bool consumeCameraPowerOffDisconnect(const char* source) {
  const int reason = bleCamera.consumeDisconnectReason();
  if (!isCameraPowerOffDisconnectReason(reason)) {
    return false;
  }

  enterCameraSleepGuard(source, reason);
  return true;
}

void clearCameraSleepGuard(const char* source) {
  if (cameraAutoWakeBlocked) {
    Serial.printf("BLE guard: clearing guard (%s), previous disconnect reason=%d\n",
                  source != nullptr ? source : "manual",
                  cameraAutoWakeDisconnectReason);
  }
  cameraPowerState = RicohCameraPowerState::Unknown;
  cameraOperationMode = RicohCameraOperationMode::Unknown;
  cameraAutoWakeBlocked = false;
  cameraPowerProbeBackoffUntil = 0;
  cameraAutoWakeDisconnectReason = 0;
  cameraRecoveryFirstFailureAt = 0;
  cameraRecoveryFailureCount = 0;
  bleCamera.clearDisconnectReason();
}

bool cameraSleepGuardBlocksFlow(const char* action) {
  (void)action;
  if (cameraAutoWakeBlocked) {
    showCameraSleepGuardStatus(false);
  }
  // Block all auto-driven flow while waiting: no auto scan/connect/WLAN-ON.
  // The only way out of the wait is a user BtnA press (requestManualCameraWake).
  return cameraAutoWakeBlocked;
}

bool hasUsableCachedWifiCredentials() {
  return cameraProfile.wifi.cached && cameraProfile.wifi.ssid.length() > 0 && cameraProfile.bleAddress.length() > 0;
}

bool wifiCredentialsMatchProfile(const RicohBleWifiCredentials& credentials) {
  if (credentials.ssid != cameraProfile.wifi.ssid ||
      credentials.passphrase != cameraProfile.wifi.passphrase) {
    return false;
  }
  if (credentials.bssid.length() > 0 && credentials.bssid != cameraProfile.wifi.bssid) {
    return false;
  }
  if (credentials.channel != 0 && cameraProfile.wifi.channel != 0 && credentials.channel != cameraProfile.wifi.channel) {
    return false;
  }
  return true;
}

void saveWifiCredentialCache(const char* source) {
  if (cameraProfile.bleAddress.length() == 0 || cameraProfile.wifi.ssid.length() == 0) {
    return;
  }
  if (profileStore.saveWifiCredentials(cameraProfile.bleAddress, cameraProfile.wifi)) {
    cameraProfile.wifi.cached = true;
    Serial.printf("WiFi cache: saved (%s) ssid='%s' bssid='%s' channel=%u freq=%u\n",
                  source != nullptr ? source : "update",
                  cameraProfile.wifi.ssid.c_str(),
                  cameraProfile.wifi.bssid.c_str(),
                  static_cast<unsigned>(cameraProfile.wifi.channel),
                  static_cast<unsigned>(cameraProfile.wifi.frequencyMhz));
  }
}

void saveConnectedWifiBssidToCache(const char* source) {
  if (!grWifi.isConnected() || cameraProfile.bleAddress.length() == 0 || cameraProfile.wifi.ssid.length() == 0) {
    return;
  }

  const String connectedBssid = grWifi.bssidString();
  if (connectedBssid.length() > 0 && connectedBssid != String("00:00:00:00:00:00")) {
    if (cameraProfile.wifi.bssid != connectedBssid) {
      Serial.printf("WiFi cache: learned BSSID '%s' from connection (%s)\n",
                    connectedBssid.c_str(),
                    source != nullptr ? source : "connected");
    }
    cameraProfile.wifi.bssid = connectedBssid;
  }
  saveWifiCredentialCache(source != nullptr ? source : "connected");
}

void applyBleWifiCredentials(const RicohBleWifiCredentials& credentials, const char* source, bool persist) {
  const bool changed = credentials.ssid != cameraProfile.wifi.ssid ||
                       credentials.passphrase != cameraProfile.wifi.passphrase ||
                       credentials.bssid != cameraProfile.wifi.bssid ||
                       (credentials.frequencyMhz != 0 && credentials.frequencyMhz != cameraProfile.wifi.frequencyMhz) ||
                       (credentials.channel != 0 && credentials.channel != cameraProfile.wifi.channel);

  cameraProfile.wifi.ssid = credentials.ssid;
  cameraProfile.wifi.passphrase = credentials.passphrase;
  cameraProfile.wifi.bssid = credentials.bssid;
  cameraProfile.wifi.frequencyMhz = credentials.frequencyMhz;
  cameraProfile.wifi.channel = credentials.channel;

  Serial.printf("WiFi params: %s ssid='%s' channel=%u freq=%u changed=%d\n",
                source != nullptr ? source : "BLE",
                cameraProfile.wifi.ssid.c_str(),
                static_cast<unsigned>(cameraProfile.wifi.channel),
                static_cast<unsigned>(cameraProfile.wifi.frequencyMhz),
                changed ? 1 : 0);
  if (persist) {
    saveWifiCredentialCache(source);
  }
}

void scheduleWifiCacheRefresh() {
  if (!bleCamera.isConnected() || cameraProfile.bleAddress.length() == 0) {
    return;
  }
  wifiCacheRefreshPending = true;
  wifiCacheRefreshAfter = millis() + WIFI_CACHE_REFRESH_DELAY_MS;
}

void refreshWifiCacheIfDue() {
  if (!wifiCacheRefreshPending || !timeReached(wifiCacheRefreshAfter)) {
    return;
  }
  wifiCacheRefreshPending = false;
  if (!bleCamera.isConnected() || cameraSleepGuardActive()) {
    return;
  }

  RicohBleWifiCredentials fresh;
  const rvf::Result refreshCredentialsResult = bleCamera.waitForWifiCredentials(fresh, RICOH_BLE_WIFI_CREDENTIAL_WAIT_MS);
  if (refreshCredentialsResult.failed()) {
    Serial.printf("WiFi cache: deferred refresh failed: %s\n", bleCamera.lastError().c_str());
    return;
  }

  if (!wifiCredentialsMatchProfile(fresh)) {
    Serial.println("WiFi cache: BLE fresh params differ from cached/runtime params; updating cache for next boot");
  }
  applyBleWifiCredentials(fresh, "deferred BLE refresh", false);
  saveConnectedWifiBssidToCache("deferred BLE refresh");
}

void applyDefaultProfile() {
  if (!profileStore.load(cameraProfile)) {
    Serial.println("Profile: NVS open failed; using runtime defaults");
    cameraProfile = CameraProfile{};
  }

  if (cameraProfile.wifi.cameraIp.isEmpty()) {
    cameraProfile.wifi.cameraIp = GR_HOST;
  }
  wifiPreview.setEndpoint(cameraProfile.wifi.cameraIp.c_str(), GR_PORT);

  Serial.printf("Profile: camera='%s' ble='%s' ip='%s'\n",
                cameraProfile.cameraName.c_str(),
                cameraProfile.bleAddress.c_str(),
                cameraProfile.wifi.cameraIp.c_str());
}

bool ensureCameraPowerReadyForWifi(const char* source) {
  if (!cameraPowerPolicy.requiresPowerCheck()) {
    return true;
  }
  if (cameraSleepGuardBlocksFlow(source != nullptr ? source : "power guard")) {
    return false;
  }
  if (!bleCamera.isConnected()) {
    cameraPowerState = RicohCameraPowerState::Unknown;
    cameraOperationMode = RicohCameraOperationMode::Unknown;
    return false;
  }

  setCameraFlowState(CameraFlowState::CheckingCameraPower, source != nullptr ? source : "check camera power");
  showStatusIfChanged("BLE_READY", "Checking power", cameraProfile.cameraName, "", true);
  RicohCameraPowerState nextState = RicohCameraPowerState::Unknown;
  bool readOk = false;
  const uint8_t retries = RICOH_BLE_POWER_READ_RETRIES == 0 ? 1 : RICOH_BLE_POWER_READ_RETRIES;
  for (uint8_t attempt = 0; attempt < retries; ++attempt) {
    const rvf::Result powerReadResult = bleCamera.readPowerState(nextState);
    if (powerReadResult.ok()) {
      readOk = true;
      break;
    }
    Serial.printf("BLE: power read attempt %u/%u failed: %s\n",
                  static_cast<unsigned>(attempt + 1),
                  static_cast<unsigned>(retries),
                  bleCamera.lastError().c_str());
    delay(120);
    yield();
  }

  cameraPowerState = readOk ? nextState : RicohCameraPowerState::Unknown;
  if (consumeCameraPowerOffNotification("BLE power notify during power check")) {
    return false;
  }

  cameraOperationMode = RicohCameraOperationMode::Unknown;
  bool operationModeReadOk = false;
  if (cameraPowerPolicy.shouldReadOperationMode(readOk, toPolicyPowerStatus(cameraPowerState))) {
    const uint8_t modeRetries = RICOH_BLE_OPERATION_MODE_READ_RETRIES == 0 ? 1 : RICOH_BLE_OPERATION_MODE_READ_RETRIES;
    for (uint8_t attempt = 0; attempt < modeRetries; ++attempt) {
      const rvf::Result operationModeResult = bleCamera.readOperationMode(cameraOperationMode);
      if (operationModeResult.ok()) {
        operationModeReadOk = true;
        break;
      }
      Serial.printf("BLE: operation mode read attempt %u/%u failed: %s\n",
                    static_cast<unsigned>(attempt + 1),
                    static_cast<unsigned>(modeRetries),
                    bleCamera.lastError().c_str());
      delay(80);
      yield();
    }

    // Camera-asleep standby (BLE_STARTUP or POWER_OFF_TRANSFER): the operation
    // mode read on this link is latched and re-reading it here will never change
    // (verified on the GR IIIx). We must NOT send WLAN-ON to wake the camera
    // (that extends the lens — unsafe). Instead route into the camera-off wait
    // path and STOP: the stick idles (no auto reconnect churn) until the user
    // powers the camera on and presses BtnA (requestManualCameraWake), which
    // makes a fresh connect. If that fresh connect reads CAPTURE it flows
    // through to Wi-Fi; if still asleep it returns here and waits again.
    if (cameraPowerPolicy.blocksStandbyOperationMode(operationModeReadOk, toPolicyOperationStatus(cameraOperationMode), false)) {
      Serial.printf("WiFi held: camera asleep (operation mode=%s power=%s); NOT sending WLAN-ON, waiting for user to power on + press BtnA source=%s\n",
                    cameraOperationModeName(cameraOperationMode),
                    cameraPowerStateName(cameraPowerState),
                    source != nullptr ? source : "");
      // enterCameraSleepGuard() emits the settled "Camera off / Turn on to
      // connect" status; no separate showStatusIfChanged needed here.
      enterCameraSleepGuard("BLE operation mode standby", RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF);
      return false;
    }

    // Require CAPTURE mode before opening Wi-Fi. Live view is a shooting-mode
    // feature, and a camera switched off mid-session is the tricky case: it keeps
    // the BLE link alive in standby with its power characteristic LATCHED at 0x01
    // (so the power check above passes) yet reports a NON-CAPTURE operation mode
    // (verified on GR IIIx, 2026-07-09: reads 0x03 "Other"). Without this gate we
    // would drive WLAN-ON and then stall up to ~15s per attempt against the dead
    // Wi-Fi AP ("Connecting... Link"). The standby modes (BLE_STARTUP /
    // POWER_OFF_TRANSFER) are already handled above; treat every remaining
    // non-CAPTURE mode (Other / Playback) the same way -- the camera is not ready
    // for live view, so route into the sleep guard now instead of opening Wi-Fi.
    // The guard's passive probe re-confirms CAPTURE on a FRESH connect before we
    // ever send WLAN-ON again, so a genuine power-on still auto-recovers.
    if (operationModeReadOk && cameraOperationMode != RicohCameraOperationMode::Capture) {
      Serial.printf("WiFi held: camera not in CAPTURE mode (mode=%s power=%s); routing to guard source=%s\n",
                    cameraOperationModeName(cameraOperationMode),
                    cameraPowerStateName(cameraPowerState),
                    source != nullptr ? source : "");
      enterCameraSleepGuard("camera not in capture mode", RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF);
      return false;
    }
  }

  if (consumeCameraPowerOffNotification("BLE power notify before WiFi allow")) {
    return false;
  }

  if (cameraPowerPolicy.mayActivateWifi(toPolicyPowerStatus(cameraPowerState))) {
    if (cameraAutoWakeBlocked) {
      clearCameraSleepGuard("camera power on");
    }
    const rvf::Result powerNotifyResult = bleCamera.enablePowerStateNotify();
    if (powerNotifyResult.failed()) {
      Serial.printf("BLE: power notify subscribe failed: %s\n", bleCamera.lastError().c_str());
    }
    if (settleAndConsumeCameraPowerOffNotification("BLE power notify before WiFi allow")) {
      return false;
    }
    return true;
  }

  if (cameraPowerPolicy.allowsWifiWhenPowerUnknown(readOk)) {
    Serial.println("BLE: power state unknown; config allows WiFi open");
    return true;
  }

  const char* reason = cameraPowerPolicy.blockedReason(readOk, toPolicyPowerStatus(cameraPowerState));
  Serial.printf("WiFi blocked: camera power state=%s readOk=%d source=%s\n",
                cameraPowerStateName(cameraPowerState),
                readOk ? 1 : 0,
                source != nullptr ? source : "");
  // enterCameraSleepGuard() emits the settled "Camera off / Turn on to connect"
  // status; no separate showStatusIfChanged needed here.
  enterCameraSleepGuard(reason, RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF);
  return false;
}

bool activateCameraWifiOverBle() {
  if (!RICOH_BLE_AUTO_WLAN_ON_BOOT) {
    return true;
  }
  if (cameraSleepGuardBlocksFlow("WiFi open")) {
    return false;
  }
  if (!bleCamera.isConnected()) {
    showStatusIfChanged("BLE not ready", "Cannot open WiFi", "", "", true);
    return false;
  }
  if (!ensureCameraPowerReadyForWifi("WiFi open")) {
    return false;
  }
  if (settleAndConsumeCameraPowerOffNotification("BLE power notify before WiFi open")) {
    return false;
  }

  showStatusIfChanged("BLE_READY", "Opening WiFi", cameraProfile.cameraName, "", true);
  setCameraFlowState(CameraFlowState::ActivatingWifi, "BLE WiFi ON");
  const rvf::Result openWifiResult = bleCamera.openWifi();
  if (openWifiResult.failed()) {
    Serial.printf("BLE: Wi-Fi open failed: %s\n", bleCamera.lastError().c_str());
    showStatusIfChanged("BLE WiFi failed", bleCamera.lastError(), "", "", true);
    setCameraFlowState(CameraFlowState::BleReady, "BLE WiFi open failed");
    return false;
  }

  showStatusIfChanged("BLE WiFi sent", "Waiting WiFi params", cameraProfile.cameraName, "", true);
  if (RICOH_BLE_POST_WLAN_ON_WAIT_MS > 0) {
    delay(RICOH_BLE_POST_WLAN_ON_WAIT_MS);
  }
  return true;
}

bool hasStoredBleIdentity() {
  return cameraProfile.bleAddress.length() > 0;
}

String displayBleName(const RicohBleDeviceInfo& info) {
  String connectedName = info.name.length() > 0 ? info.name : preferredBleName();
  if (connectedName.isEmpty()) {
    connectedName = "RICOH GR";
  }
  return connectedName;
}

bool hasAdvertisedCameraIdentity(const RicohBleDeviceInfo& info) {
  return info.name.length() > 0 ||
         info.hasInfoService ||
         info.hasCameraService ||
         info.hasShootingService ||
         info.hasControlService;
}

bool shouldSkipWeakStoredIdentityCandidate(const RicohBleDeviceInfo& info, bool firstBootPairing) {
  return !firstBootPairing &&
         hasStoredBleIdentity() &&
         info.address.equalsIgnoreCase(cameraProfile.bleAddress) &&
         !hasAdvertisedCameraIdentity(info);
}

bool shouldDelayStoredIdentityPowerProbe(const RicohBleDeviceInfo& info, bool firstBootPairing) {
  return !firstBootPairing &&
         hasStoredBleIdentity() &&
         info.address.equalsIgnoreCase(cameraProfile.bleAddress) &&
         cameraPowerProbeBackoffActive();
}

bool shouldBackoffAfterStoredIdentityConnectFailure(const String& errorText, bool firstBootPairing) {
  if (firstBootPairing || !hasStoredBleIdentity()) {
    return false;
  }
  return errorText.indexOf("security") >= 0 ||
         errorText.indexOf("Security") >= 0 ||
         errorText.indexOf("BLE lost during security") >= 0;
}

void deferStoredIdentityPowerProbeAfterConnectFailure(const String& errorText) {
  cameraPowerState = RicohCameraPowerState::Unknown;
  cameraOperationMode = RicohCameraOperationMode::Unknown;
  cameraAutoWakeBlocked = true;
  if (cameraAutoWakeDisconnectReason == 0) {
    cameraAutoWakeDisconnectReason = RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF;
  }
  scheduleCameraPowerProbeBackoff(errorText.c_str());
}

void saveConnectedBleIdentity(const String& connectedName, const RicohBleDeviceInfo& info) {
  cameraProfile.cameraName = connectedName;
  cameraProfile.bleAddress = info.address;
  cameraProfile.bleAddressType = info.addressType;
  cameraProfile.bleAddressTypeKnown = true;
  cameraProfile.bleBonded = bleCamera.isBonded(info);
  profileStore.saveBleIdentity(cameraProfile.cameraName,
                               cameraProfile.bleAddress,
                               cameraProfile.bleAddressType,
                               cameraProfile.bleBonded);
}

bool serviceButtonsDuringBleOperation() {
  const ButtonEvents events = buttons.poll();
  // BLE scan/connect waits block the main loop for seconds at a time, which is
  // exactly when the status screen is on-screen. Poll orientation here too
  // (self-throttled to PREVIEW_ORIENTATION_POLL_MS) so the display keeps
  // rotating while we're stuck in the BLE busy-loop, not just during live view.
  updatePreviewOrientation();
  // The camera-off wait state blocks the main loop for up to ~2.5s per passive
  // awake scan -- exactly when the user is most likely to double-click PWR to
  // switch the stick off. The main-loop pollStickPowerHold() only runs between
  // scans, so both press edges of a <700ms double-click land inside the blocked
  // window and get missed. Sample the power button here too (self-throttled to
  // POWER_BUTTON_POLL_MS) so the shutdown gesture works from the status screen.
  if (pollStickPowerHold()) {
    shutdownStickS3();  // terminal: powers off / never returns
  }
  if (!events.resetPairing) {
    return false;
  }

  key2PairingResetRequested = true;
  Serial.println("BLE pairing reset: requested during BLE operation");
  return true;
}

// On-device BLE passkey entry (button-driven). The GR IIIx displays a random
// 6-digit code each pairing attempt; the user keys it in on the StickS3:
//   BtnA short tap    = increment the current digit (0-9, wraps)
//   BtnA hold         = lock the digit and advance; the lock fires the instant
//                       PASSKEY_ADVANCE_HOLD_MS elapses WHILE the button is
//                       still down, not on release, so the user gets immediate
//                       feedback and can just let go.
// After the 6th digit is locked the code is committed and injected.
uint8_t g_pinDigits[6] = {0, 0, 0, 0, 0, 0};
uint8_t g_pinPos = 0;
uint32_t g_pinPressStartMs = 0;
bool g_pinPressActive = false;
bool g_pinAdvanceFired = false;

bool providePairingPasskey(bool firstCall, uint32_t& outCode) {
  if (firstCall) {
    for (uint8_t i = 0; i < 6; ++i) {
      g_pinDigits[i] = 0;
    }
    g_pinPos = 0;
    g_pinPressActive = false;
    g_pinAdvanceFired = false;
    ui.showPasskeyEntry(g_pinDigits, g_pinPos);
    return false;
  }

  // M5.update() is driven each iteration by serviceButtonsDuringBleOperation()
  // (buttons.poll()), which the security-wait loop calls right before us. Read
  // the latched BtnA edges here WITHOUT calling M5.update() again, otherwise the
  // second update would swallow the press/release events.
  if (M5.BtnA.wasPressed()) {
    g_pinPressStartMs = millis();
    g_pinPressActive = true;
    g_pinAdvanceFired = false;
  }

  // Fire the advance while the button is still held, the moment the hold
  // threshold is reached. This provider is polled every ~10-20ms by the
  // security-wait loop, so the lock feels immediate rather than waiting for
  // the user to release.
  if (g_pinPressActive && !g_pinAdvanceFired &&
      (millis() - g_pinPressStartMs) >= PASSKEY_ADVANCE_HOLD_MS) {
    g_pinAdvanceFired = true;
    ++g_pinPos;
    if (g_pinPos >= 6) {
      uint32_t code = 0;
      for (uint8_t i = 0; i < 6; ++i) {
        code = code * 10 + (g_pinDigits[i] % 10);
      }
      outCode = code;
      ui.showPasskeyEntry(g_pinDigits, 6);
      return true;
    }
    ui.showPasskeyEntry(g_pinDigits, g_pinPos);
  }

  if (g_pinPressActive && M5.BtnA.wasReleased()) {
    g_pinPressActive = false;
    // A release before the hold threshold is a short tap: increment the digit.
    // If the hold already advanced, the release is a no-op.
    if (!g_pinAdvanceFired) {
      g_pinDigits[g_pinPos] = (g_pinDigits[g_pinPos] + 1) % 10;
      ui.showPasskeyEntry(g_pinDigits, g_pinPos);
    }
    g_pinAdvanceFired = false;
  }
  return false;
}

bool resetBlePairingIfRequested() {
  if (!key2PairingResetRequested) {
    return false;
  }
  key2PairingResetRequested = false;
  resetBlePairingFromKey2();
  return true;
}

bool runBleDiscoveryAtBoot() {
  if (cameraSleepGuardBlocksFlow("BLE discovery")) {
    return false;
  }
  const bool firstBootPairing = !hasStoredBleIdentity();
  const uint8_t configuredAttempts = firstBootPairing
                                       ? FIRST_BOOT_BLE_PAIRING_ATTEMPTS
                                       : (setupCameraFlowActive ? 1 : BLE_CONNECT_ATTEMPTS);
  const uint8_t attempts = configuredAttempts == 0 ? 1 : configuredAttempts;
  uint8_t consecutiveConnectFailures = 0;
  // Consecutive BLE connect failures to a known camera in THIS discovery run.
  // Once it hits BLE_STANDBY_RECONNECT_MAX_CONNECT_FAILS we conclude the camera
  // is asleep and enter the sleep guard instead of churning the whole attempt
  // budget (see config.h).
  uint8_t storedIdentityConnectFails = 0;

  if (firstBootPairing) {
    Serial.printf("BLE: no stored identity; pairing scan up to %u rounds\n", static_cast<unsigned>(attempts));
  } else if (setupCameraFlowActive) {
    Serial.printf("BLE: stored identity; setup quick scan preferred address='%s' name='%s'\n",
                  cameraProfile.bleAddress.c_str(),
                  preferredBleName().c_str());
  } else {
    Serial.printf("BLE: stored identity; scanning preferred address='%s' name='%s'\n",
                  cameraProfile.bleAddress.c_str(),
                  preferredBleName().c_str());
  }

#if RICOH_BLE_AUTO_PROBE
  // Passive advert power-bit gate. A stored camera that gets switched off keeps
  // advertising a standby beacon, so the scan-and-connect loop below would still
  // "find" it and then burn its whole attempt budget on connect-establishment
  // failures (~30s of "Connecting..."). Before spending any connect attempt,
  // sample the manufacturer-data power bit (…03 01 = awake, …03 00 = asleep):
  // if the camera is not advertising AWAKE, it is off/standby -> go straight to
  // the sleep guard. This never opens a connection, so it cannot extend the lens
  // or churn the BLE stack.
  //
  // Skipped for first-boot pairing (no stored beacon to sample) and once after a
  // user/auto wake that already confirmed the camera is on (bleSkipAwakeGateOnce).
  const bool skipAwakeGate = bleSkipAwakeGateOnce;
  bleSkipAwakeGateOnce = false;
  if (!firstBootPairing && !skipAwakeGate && cameraProfile.bleAddress.length() > 0) {
    RicohBleDeviceInfo awakeProbe;
    awakeProbe.found = true;
    awakeProbe.address = cameraProfile.bleAddress;
    awakeProbe.addressType = cameraProfile.bleAddressType;
    awakeProbe.name = preferredBleName();

    showCameraSleepGuardStatus(true);
    int advPowerBit = -1;
    const bool awake =
        bleCamera.waitForCameraAwake(awakeProbe, RICOH_BLE_AWAKE_SCAN_WINDOW_MS, &advPowerBit);
    firstAwakeScanDone = true;
    if (!awake) {
      Serial.printf(
          "BLE: advert power bit not AWAKE (bit=%d) before connect -> camera off, entering guard\n",
          advPowerBit);
      enterCameraSleepGuard("advert power bit asleep",
                            RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF);
      return false;
    }
    Serial.printf("BLE: advert power bit AWAKE (bit=0x%02X) -> proceeding to connect\n",
                  advPowerBit >= 0 ? static_cast<unsigned>(advPowerBit) : 0);
  }
#endif  // RICOH_BLE_AUTO_PROBE

  for (uint8_t attempt = 1; attempt <= attempts; ++attempt) {
    if (resetBlePairingIfRequested()) {
      return false;
    }
    bool skipRetryDelay = false;
    const String bleName = preferredBleName();
    setCameraFlowState(CameraFlowState::ScanningCamera, "BLE discovery");
    if (!firstBootPairing && !firstAwakeScanDone) {
      // Boot / first-probe transient: we have a stored camera but don't yet know
      // if it is powered on. Show "Checking camera" (display.cpp -> CHECKING...)
      // rather than "CONNECTING..." so an off camera never looks like a stall.
      showCameraSleepGuardStatus(true);
    } else {
      showStatusIfChanged(firstBootPairing ? "Pairing GR BLE" : "Scanning GR BLE",
                          cameraProfile.bleAddress,
                          bleName,
                          String("Attempt ") + attempt + "/" + attempts,
                          true);
    }

    RicohBleDeviceInfo info = bleCamera.scanCamera(cameraProfile.bleAddress, bleName, BLE_SCAN_SECONDS);
    if (resetBlePairingIfRequested()) {
      return false;
    }
    if (!info.found) {
      showStatusIfChanged("BLE not found", "Retrying...", "", "", true);
    } else if (!info.connectable) {
      Serial.printf("BLE: scan selected non-connectable candidate name='%s' addr=%s; retrying\n",
                    info.name.c_str(),
                    info.address.c_str());
      showStatusIfChanged("BLE not connectable", info.address, "Retrying...", "", true);
    } else if (shouldDelayStoredIdentityPowerProbe(info, firstBootPairing)) {
      Serial.printf("BLE: skipping standby power probe for %lums addr=%s name='%s'\n",
                    static_cast<unsigned long>(cameraPowerProbeBackoffRemainingMs()),
                    info.address.c_str(),
                    info.name.c_str());
      firstAwakeScanDone = true;  // scan determined the camera is off
      showCameraSleepGuardStatus(true);
    } else if (shouldSkipWeakStoredIdentityCandidate(info, firstBootPairing)) {
      Serial.printf("BLE: weak stored-address candidate addr=%s rssi=%d has_name=%d services=%d%d%d%d; waiting for camera power on\n",
                    info.address.c_str(),
                    info.rssi,
                    info.name.length() > 0 ? 1 : 0,
                    info.hasInfoService ? 1 : 0,
                    info.hasCameraService ? 1 : 0,
                    info.hasShootingService ? 1 : 0,
                    info.hasControlService ? 1 : 0);
      firstAwakeScanDone = true;  // scan determined the camera is off
      showCameraSleepGuardStatus(true);
    } else {
      const String connectedName = displayBleName(info);

      showStatusIfChanged("BLE camera found", connectedName, info.address, "Connecting...", true);
      setCameraFlowState(CameraFlowState::ConnectingBle, "BLE scan candidate");
      RicohBleConnectOptions options;
      options.timeoutMs = BLE_CONNECT_TIMEOUT_MS;
      options.securityWaitMs = RICOH_BLE_SECURITY_WAIT_MS;
      options.preConnectDelayMs = BLE_SCAN_TO_CONNECT_DELAY_MS;
      options.exchangeMtu = false;

      const rvf::Result connectResult = bleCamera.connectCamera(info, options);
      if (resetBlePairingIfRequested()) {
        return false;
      }
      if (connectResult.ok()) {
        saveConnectedBleIdentity(connectedName, info);
        showStatusIfChanged("BLE link ready", cameraProfile.cameraName, info.address, "WiFi via BLE", true);
        setCameraFlowState(CameraFlowState::BleReady, "BLE connected");
        return true;
      }

      Serial.printf("BLE: connect attempt %u/%u failed: %s\n",
                    static_cast<unsigned>(attempt),
                    static_cast<unsigned>(attempts),
                    bleCamera.lastError().c_str());
      if (shouldBackoffAfterStoredIdentityConnectFailure(bleCamera.lastError(), firstBootPairing)) {
        deferStoredIdentityPowerProbeAfterConnectFailure(bleCamera.lastError());
      }
      // Decode the GAP disconnect reason behind this connect failure.
      const int connectFailReason = bleCamera.consumeDisconnectReason();

      // A bonded, stored camera that refuses the connection with a remote
      // power-off reason (0x213 RemoteUserTerminated / 0x215) has gone to sleep.
      // NOTE (verified on GR IIIx, 2026-07-09 HW capture): the standby beacon
      // keeps advertising power bit 0x01 ("awake") for a long time after the
      // camera is switched off, so neither the scan nor the advertisement power
      // bit can tell it is off -- but the camera actively REFUSES the link. That
      // refusal is the definitive, prompt off-signal. Enter the guard on the
      // FIRST such failure instead of cycling connect/backoff for ~30s (which the
      // user saw as a "Connecting..." <-> "Camera off" flicker). Previously this
      // reason was consumed and IGNORED pre-ready, which caused the churn.
      if (!firstBootPairing && hasStoredBleIdentity() &&
          isCameraPowerOffDisconnectReason(connectFailReason)) {
        Serial.printf("BLE: reconnect refused by camera (reason=%d 0x%03X) -> camera off, entering guard\n",
                      connectFailReason, connectFailReason);
        bleCamera.disconnect();
        enterCameraSleepGuard("reconnect refused -- camera off", connectFailReason);
        return false;
      }

      // Past-ready power-protected flow: honor the guard entry for the same
      // remote power-off reasons (mirrors consumeCameraPowerOffDisconnectAfterReady).
      if (appController.isPowerProtectedFlowState() &&
          isCameraPowerOffDisconnectReason(connectFailReason)) {
        enterCameraSleepGuard("BLE connect failed", connectFailReason);
        return false;
      }

      // Fallback: a RUN of connect failures to a known camera for any other
      // reason (e.g. repeated 0x208 SupervisionTimeout / 0x216 without a clean
      // power-off reason) still means it is unreachable. Enter the guard rather
      // than burning the whole attempt budget -- the guard's passive probe
      // handles the eventual power-on.
      if (!firstBootPairing && hasStoredBleIdentity() &&
          ++storedIdentityConnectFails >= BLE_STANDBY_RECONNECT_MAX_CONNECT_FAILS) {
        Serial.printf("BLE: %u consecutive reconnect failures to stored camera; treating as camera off\n",
                      static_cast<unsigned>(storedIdentityConnectFails));
        bleCamera.disconnect();
        enterCameraSleepGuard("reconnect failed -- camera likely asleep",
                              RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF);
        return false;
      }
      showStatusIfChanged("BLE connect failed", bleCamera.lastError(), "Retrying...", "", true);
      bleCamera.disconnect();
      if (bleCamera.lastFailureWasResourceExhausted()) {
        Serial.println("BLE: host resources exhausted during connect; reset stack before retry");
        // clearObjects=true: NimBLE can leave a NimBLEClient stuck/un-freed when a
        // connect fails mid security handshake (observed live: repeated
        // "BLE security timeout"/"BLE lost during security" failures each leaked
        // one client, and resetStack(false) does NOT reclaim them — after 3 leaks
        // NimBLEDevice::createClient() permanently fails with "already at max: 3"
        // until a full power cycle). Force a full object clear here so automatic
        // retries can actually recover instead of wedging the stack.
        bleCamera.resetStack(true);
        consecutiveConnectFailures = 0;
        skipRetryDelay = true;
      } else {
        consecutiveConnectFailures++;
        if (BLE_STACK_RESET_AFTER_FAILURES > 0 && consecutiveConnectFailures >= BLE_STACK_RESET_AFTER_FAILURES) {
          // Same leaked-client risk as above after repeated non-resource-exhausted
          // failures (e.g. security timeouts) — clear objects on this reset too.
          bleCamera.resetStack(true);
          consecutiveConnectFailures = 0;
          skipRetryDelay = true;
        }
      }
    }

    if (attempt < attempts && !skipRetryDelay) {
      delay(BLE_CONNECT_RETRY_DELAY_MS);
      yield();
      if (resetBlePairingIfRequested()) {
        return false;
      }
    }
  }

  showStatusIfChanged("BLE unavailable", "Return BLE scan", preferredBleName(), "", true);
  setCameraFlowState(CameraFlowState::BleScan, "BLE attempts exhausted");
  // Full attempt budget exhausted -- clear any leaked NimBLEClient objects before
  // falling back to BleScan so the next round starts from a clean slate.
  bleCamera.resetStack(true);
  return false;
}

bool bleStillConnectedForWifi() {
  return bleCamera.isConnected();
}

bool wifiStillConnectedForController() {
  return grWifi.isConnected();
}

bool connectWifiFromProfile(bool forceStatus, bool requireBleAnchor = false, uint32_t totalTimeoutMs = WIFI_CONNECT_TIMEOUT_MS, bool allowFullScanFallback = true) {
  wifiPreview.setEndpoint(cameraProfile.wifi.cameraIp.c_str(), GR_PORT);
  showStatusIfChanged("Connecting WiFi", cameraProfile.wifi.ssid, cameraProfile.wifi.cameraIp, "", forceStatus);
  Serial.printf("WiFi: connecting ssid='%s' bssid='%s' channel=%u freq=%u\n",
                cameraProfile.wifi.ssid.c_str(),
                cameraProfile.wifi.bssid.c_str(),
                static_cast<unsigned>(cameraProfile.wifi.channel),
                static_cast<unsigned>(cameraProfile.wifi.frequencyMhz));

  bool connected = false;
  const uint8_t channel = cameraProfile.wifi.channel;
  const uint32_t hintTimeout = WIFI_CHANNEL_HINT_CONNECT_TIMEOUT_MS < totalTimeoutMs
                                 ? WIFI_CHANNEL_HINT_CONNECT_TIMEOUT_MS
                                 : totalTimeoutMs;

  if (channel != 0) {
    connected = wifiPreview.connectWifi(cameraProfile.wifi.ssid.c_str(),
                                        cameraProfile.wifi.passphrase.c_str(),
                                        cameraProfile.wifi.bssid.c_str(),
                                        channel,
                                        hintTimeout,
                                        requireBleAnchor ? bleStillConnectedForWifi : nullptr)
                  .ok();

    if (allowFullScanFallback && !connected && (!requireBleAnchor || bleStillConnectedForWifi())) {
      uint32_t fallbackTimeout = totalTimeoutMs > hintTimeout ? totalTimeoutMs - hintTimeout : totalTimeoutMs;
      if (fallbackTimeout < 1000) {
        fallbackTimeout = totalTimeoutMs;
      }
      Serial.printf("WiFi: channel hint %u failed; fallback to full scan timeout=%lums\n",
                    static_cast<unsigned>(channel),
                    static_cast<unsigned long>(fallbackTimeout));
      connected = wifiPreview.connectWifi(cameraProfile.wifi.ssid.c_str(),
                                          cameraProfile.wifi.passphrase.c_str(),
                                          cameraProfile.wifi.bssid.c_str(),
                                          0,
                                          fallbackTimeout,
                                          requireBleAnchor ? bleStillConnectedForWifi : nullptr)
                    .ok();
    }
  } else {
    connected = wifiPreview.connectWifi(cameraProfile.wifi.ssid.c_str(),
                                        cameraProfile.wifi.passphrase.c_str(),
                                        cameraProfile.wifi.bssid.c_str(),
                                        0,
                                        totalTimeoutMs,
                                        requireBleAnchor ? bleStillConnectedForWifi : nullptr)
                  .ok();
  }

  if (connected) {
    showStatusIfChanged("WiFi connected", grWifi.localIPString(), cameraProfile.wifi.cameraIp, "", true);
    Serial.printf("WiFi: connected ip=%s rssi=%ld\n", grWifi.localIPString().c_str(), static_cast<long>(grWifi.rssi()));
    return true;
  }

  showStatusIfChanged("WiFi pending", grWifi.statusText(), cameraProfile.wifi.ssid, "", true);
  Serial.printf("WiFi: connect failed status=%s\n", grWifi.statusText().c_str());
  return false;
}

bool fetchCameraPropsForController() {
  const rvf::Result propsResult = wifiPreview.fetchProps(pendingCameraProps, PROPS_TIMEOUT_MS);
  if (propsResult.failed()) {
    return false;
  }
  return true;
}

void onHttpProbeFailedForController() {
  Serial.printf("HTTP: props probe failed: %s\n", wifiPreview.lastError().c_str());
  showStatusIfChanged("HTTP probe failed", wifiPreview.lastError(), "Back to BLE scan", "", true);
}

void onHttpProbeSucceededForController() {
  cameraProps = pendingCameraProps;
  lastPropsAt = millis();
  Serial.printf("HTTP: camera ready model='%s' battery='%s'\n", cameraProps.model.c_str(), cameraProps.battery.c_str());
  showStatusIfChanged("HTTP Probe OK", cameraProps.model, cameraProps.battery, "LiveView next", true);
}

void showStartingLiveViewForController() {
  showStatusIfChanged("Starting LiveView", grWifi.localIPString(), cameraProps.model, cameraProps.battery, true);
}

bool openLiveViewForController() {
  const rvf::Result previewResult = wifiPreview.startPreview();
  if (previewResult.failed()) {
    return false;
  }
  return true;
}

void onLiveViewOpenFailedForController() {
  Serial.printf("LiveView: open failed: %s\n", wifiPreview.lastError().c_str());
  showStatusIfChanged("LiveView failed", wifiPreview.lastError(), "Back to BLE scan", "", true);
}

void onLiveViewOpenedForController() {
  lastFrameAt = millis();
  lastLiveViewActivityAt = lastFrameAt;
  previewFrameBuffer.resetRuntimeStats();
  Serial.println("LiveView: connected");
}

bool cameraRecoveryInProgressForController() {
  return cameraRecoveryInProgress;
}

uint32_t lastCameraRecoveryAtForController() {
  return lastCameraRecoveryAt;
}

void setLastCameraRecoveryAtForController(uint32_t timestampMs) {
  lastCameraRecoveryAt = timestampMs;
}

void setCameraRecoveryInProgressForController(bool inProgress) {
  cameraRecoveryInProgress = inProgress;
}

void showRecoveryBleReadyRetryForController(const char* reason) {
  showStatusIfChanged("Camera recovery", reason != nullptr ? reason : "reconnect", "BLE_READY retry", "", true);
}

void showRecoveryBleScanForController(const char* reason) {
  showStatusIfChanged("Camera recovery", reason != nullptr ? reason : "reconnect", "Back to BLE scan", "", true);
}

void shortRecoveryDelayForController() {
  delay(100);
}

void onRecoveryGuardBlockedForController() {
  lastFrameAt = millis();
  lastCameraRecoveryAt = millis();
  cameraRecoveryInProgress = false;
}

void onRecoveryFinishedForController(bool recovered) {
  if (recovered) {
    cameraRecoveryFirstFailureAt = 0;
    cameraRecoveryFailureCount = 0;
    lastFrameAt = millis();
    lastCameraRecoveryAt = millis();
    cameraRecoveryInProgress = false;
    return;
  }

  // Recovery failed. Some GR IIIx power-off events never surface a clean remote
  // disconnect reason (0x213/0x215) -- the link just dies and the camera's
  // Wi-Fi AP vanishes -- so consumeCameraPowerOffDisconnect() never routes us
  // into the sleep guard and we would otherwise loop on "Connecting..." for as
  // long as the camera stays off. Bounded fallback: once BLE has been
  // unreachable across CAMERA_OFF_RECOVERY_FALLBACK_ATTEMPTS consecutive
  // recoveries OR for CAMERA_OFF_RECOVERY_FALLBACK_MS, conclude the camera is
  // off and enter the guard so the screen settles on "Camera off" and the BLE
  // stack stops churning. A transient glitch recovers before the threshold
  // (the scan re-finds a still-powered camera), so this won't misfire.
  const uint32_t nowMs = millis();
  if (cameraRecoveryFirstFailureAt == 0) {
    cameraRecoveryFirstFailureAt = nowMs;
  }
  if (cameraRecoveryFailureCount < 0xFFFF) {
    cameraRecoveryFailureCount++;
  }

  const bool bleDown = !bleCamera.isConnected();
  const bool exhaustedAttempts =
      cameraRecoveryFailureCount >= CAMERA_OFF_RECOVERY_FALLBACK_ATTEMPTS;
  const bool exhaustedTime =
      static_cast<uint32_t>(nowMs - cameraRecoveryFirstFailureAt) >=
      CAMERA_OFF_RECOVERY_FALLBACK_MS;

  if (bleDown && (exhaustedAttempts || exhaustedTime) && !cameraSleepGuardActive()) {
    Serial.printf(
        "Camera recovery: giving up after %u attempts / %lums with BLE down; "
        "treating as camera off\n",
        static_cast<unsigned>(cameraRecoveryFailureCount),
        static_cast<unsigned long>(nowMs - cameraRecoveryFirstFailureAt));
    cameraRecoveryFirstFailureAt = 0;
    cameraRecoveryFailureCount = 0;
    // reason 0: this is an inferred power-off (no clean 0x213/0x215 seen).
    enterCameraSleepGuard("recovery exhausted -- camera likely off", 0);
    cameraRecoveryInProgress = false;
    return;
  }

  lastFrameAt = millis();
  lastCameraRecoveryAt = 0;
  cameraRecoveryInProgress = false;
}

void requestManualCameraWakeForController(const char* source) {
  requestManualCameraWake(source);
}

bool shutterReadyForController() {
  return bleCamera.shutterReady();
}

void showShutterBleNotReadyForController() {
  showStatusIfChanged("Button A shutter", "BLE not ready", "Back to BLE scan", "", true);
}

bool shootAutofocusForController() {
  // While the live preview is on screen, shoot silently: overwriting the canvas
  // with a status card makes the preview visibly blink for a split second on
  // every shot. Only fall back to the status screen when there's no live feed.
  if (!wifiPreview.isPreviewRunning()) {
    showStatusIfChanged("Button A shutter", "Shooting...", cameraProps.model, cameraProps.battery, true);
  } else {
    // Hold the current live frame across the exposure so the camera's black
    // capture frames never blink onto the screen.
    previewFreezeUntil = millis() + PREVIEW_CAPTURE_FREEZE_MS;
  }
  const rvf::Result shootResult = bleCamera.shoot(true);
  return shootResult.ok();
}

void onShutterOkForController() {
  if (!wifiPreview.isPreviewRunning()) {
    showStatusIfChanged("Button A shutter", "SHOT OK", cameraProps.model, cameraProps.battery, true);
  }
}

void onShutterFailedForController() {
  Serial.printf("Button A: BLE shutter failed: %s\n", bleCamera.lastError().c_str());
  showStatusIfChanged("Button A BLE failed", bleCamera.lastError(), "Preview kept", "", true);
}

bool previewKeptAfterShutterFailureForController() {
  return appController.isPreviewActive() && grWifi.isConnected() && wifiPreview.isPreviewRunning();
}

void showCameraSleepGuardStatusForController() {
  showCameraSleepGuardStatus(false);
}

bool previewStreamRunningForController() {
  return wifiPreview.isPreviewRunning();
}

bool readAndProcessLiveViewFrameForController() {
  const int readLen = wifiPreview.readFrame(streamReadBuffer, sizeof(streamReadBuffer));
  if (readLen > 0) {
    lastLiveViewActivityAt = millis();
    liveViewReadErrorSince = 0;  // healthy stream; clear any pending grace
    wifiPreview.processFrameData(streamReadBuffer, static_cast<size_t>(readLen));
    return true;
  }
  if (readLen < 0) {
    // Capturing a photo briefly interrupts the HTTP LiveView stream. Rather than
    // tear the stream down (which blanks the preview), hold the last decoded
    // frame and keep trying for a short grace period. onJpegFrame only pushes on
    // a successful decode, so the last good frame stays on screen meanwhile.
    const uint32_t now = millis();
    if (liveViewReadErrorSince == 0) {
      liveViewReadErrorSince = now;
    }
    if (now - liveViewReadErrorSince >= LIVEVIEW_READ_ERROR_GRACE_MS) {
      Serial.printf("LiveView: read failed for %lums: %s\n",
                    static_cast<unsigned long>(now - liveViewReadErrorSince),
                    wifiPreview.lastError().c_str());
      liveViewReadErrorSince = 0;
      return false;  // persistent failure -> let the controller reconnect
    }
    return true;  // transient (likely capture hiccup) -> hold last frame
  }
  // readLen == 0: no data available yet (normal between frames / brief pause).
  return true;
}

// LiveView perf diagnostics: [PREVIEW] gives fps/read_ms/mjpeg_cb_ms/decode_ms/
// render_ms, [FRAME] gives buffer occupancy + dropped-frame counts. Both are
// logged on the same 5s cadence so the two lines land together in the serial
// monitor — compare read_ms (network-bound) vs decode_ms+render_ms
// (CPU-bound) to see which stage is actually limiting the refresh rate
// before tuning STREAM_READ_BUFFER_SIZE / JPEG_SCALE_POLICY further.
void logPreviewStatsForController() {
  wifiPreview.logStatsIfDue(millis());
  previewFrameBuffer.syncStreamStats(mjpeg.frames(), mjpeg.droppedFrames(), mjpeg.currentLength());
  previewFrameBuffer.logStatsIfDue(millis(), 5000);
}

uint32_t lastFrameAtForController() {
  return lastFrameAt;
}

uint32_t lastLiveViewActivityAtForController() {
  return lastLiveViewActivityAt;
}

bool reasonRequiresBleRescan(const char* reason) {
  if (reason == nullptr) {
    return !bleCamera.isConnected();
  }
  const String text(reason);
  // Losing the live stream is the definitive camera power-off signal: the GR only
  // drops its AP / stops the MJPEG feed when it powers off or sleeps. But the BLE
  // link often stays "connected" in standby and returns LATCHED awake values
  // (power=On, mode=CAPTURE from when the session was live), so resuming WLAN-ON
  // on that stale link neither detects the power-off nor restores the stream --
  // resumeFromBleReady just churns WiFi opens for ~30s ("Connecting... Link")
  // until the BLE supervision timeout fires. Force a FRESH reconnect instead: a
  // new link (via runBleDiscoveryAtBoot) first samples the advertisement power
  // bit -- an off camera reads asleep and routes straight into the sleep guard --
  // rather than trusting the latched connection state. This is the "fresh
  // reconnect is the only reliable state read" principle the power gate relies on.
  //
  // This covers every way the stream can drop while the stale BLE link lingers:
  // "WiFi disconnected", "LiveView closed", "LiveView read failed" and
  // "LiveView stall watchdog". These only fire after the photo-capture blackout
  // ride-out (see readAndProcessLiveViewFrameForController), so a brief capture
  // pause never triggers a full rescan -- only a genuine power-off does.
  return !bleCamera.isConnected() ||
         text.indexOf("BLE disconnected") >= 0 ||
         text.indexOf("BLE not ready") >= 0 ||
         text.indexOf("WiFi disconnected") >= 0 ||
         text.indexOf("LiveView") >= 0;
}

void disconnectWifiLiveViewToBleReady(const char* reason) {
  closeLiveView(reason != nullptr ? reason : "BLE_READY reset");
  wifiPreview.disconnectWifi();
  if (bleCamera.isConnected()) {
    setCameraFlowState(CameraFlowState::BleReady, reason != nullptr ? reason : "BLE still connected");
  } else {
    setCameraFlowState(CameraFlowState::BleScan, reason != nullptr ? reason : "BLE lost");
  }
}

void disconnectAllTransportsToBleScan(const char* reason) {
  closeLiveView(reason != nullptr ? reason : "flow reset");
  wifiPreview.disconnectWifi();
  bleCamera.disconnect();
  setCameraFlowState(CameraFlowState::BleScan, reason);
}

void resetBleStackBeforeScanAfterLinkLoss(const char* reason) {
  Serial.printf("BLE recovery: reset stack (%s)\n", reason != nullptr ? reason : "link lost");
  showStatusIfChanged("BLE stack reset", "Camera link lost", preferredBleName(), "Scanning soon", true);
  bleCamera.resetStack();
  delay(BLE_RECOVERY_STACK_RESET_GRACE_MS);
  yield();
}

void delayAndYield(uint32_t delayMs) {
  delay(delayMs);
  yield();
}

void disconnectWifiForController() {
  wifiPreview.disconnectWifi();
}

void clearBleDisconnectReasonForController() {
  bleCamera.clearDisconnectReason();
}

bool connectCachedWifiFromProfileForController() {
  if (WIFI_CACHED_CONNECT_GRACE_MS > 0) {
    Serial.printf("WiFi cache: waiting %lums for camera AP before cached connect\n",
                  static_cast<unsigned long>(WIFI_CACHED_CONNECT_GRACE_MS));
    delay(WIFI_CACHED_CONNECT_GRACE_MS);
    yield();
  }
  Serial.printf("WiFi cache: trying cached params ssid='%s' bssid='%s' channel=%u short_timeout=%lums\n",
                cameraProfile.wifi.ssid.c_str(),
                cameraProfile.wifi.bssid.c_str(),
                static_cast<unsigned>(cameraProfile.wifi.channel),
                static_cast<unsigned long>(WIFI_CACHED_CONNECT_TIMEOUT_MS));
  return connectWifiFromProfile(true, true, WIFI_CACHED_CONNECT_TIMEOUT_MS, false);
}

void onCachedWifiConnectedForController() {
  saveConnectedWifiBssidToCache("cached connect");
  scheduleWifiCacheRefresh();
}

void onCachedWifiConnectFailedForController() {
  Serial.println("WiFi cache: cached connect failed; reading fresh BLE params");
}

bool readFreshWifiCredentialsForController() {
  const rvf::Result wifiCredentialsResult = bleCamera.waitForWifiCredentials(pendingFreshWifiCredentials, RICOH_BLE_WIFI_CREDENTIAL_WAIT_MS);
  if (wifiCredentialsResult.failed()) {
    showStatusIfChanged("BLE WiFi params", bleCamera.lastError(), "Back to BLE_READY", "", true);
    return false;
  }
  return true;
}

void applyFreshWifiCredentialsForController() {
  applyBleWifiCredentials(pendingFreshWifiCredentials, "fresh BLE", false);
}

bool connectFreshWifiFromProfileForController() {
  return connectWifiFromProfile(true, true);
}

void onFreshWifiConnectedForController() {
  saveConnectedWifiBssidToCache("fresh BLE connect");
}

rvf::AppFlowActions makeAppFlowActions() {
  rvf::AppFlowActions actions;
  actions.cameraSleepGuardBlocksFlow = cameraSleepGuardBlocksFlow;
  actions.runBleDiscovery = runBleDiscoveryAtBoot;
  actions.cameraSleepGuardActive = cameraSleepGuardActive;
  actions.activateCameraWifiOverBle = activateCameraWifiOverBle;
  actions.disconnectWifi = disconnectWifiForController;
  actions.clearBleDisconnectReason = clearBleDisconnectReasonForController;
  actions.hasUsableCachedWifiCredentials = hasUsableCachedWifiCredentials;
  actions.connectCachedWifiFromProfile = connectCachedWifiFromProfileForController;
  actions.onCachedWifiConnected = onCachedWifiConnectedForController;
  actions.onCachedWifiConnectFailed = onCachedWifiConnectFailedForController;
  actions.readFreshWifiCredentials = readFreshWifiCredentialsForController;
  actions.applyFreshWifiCredentials = applyFreshWifiCredentialsForController;
  actions.connectFreshWifiFromProfile = connectFreshWifiFromProfileForController;
  actions.onFreshWifiConnected = onFreshWifiConnectedForController;
  actions.isWifiConnected = wifiStillConnectedForController;
  actions.fetchCameraProps = fetchCameraPropsForController;
  actions.onHttpProbeSucceeded = onHttpProbeSucceededForController;
  actions.onHttpProbeFailed = onHttpProbeFailedForController;
  actions.showStartingLiveView = showStartingLiveViewForController;
  actions.openLiveView = openLiveViewForController;
  actions.onLiveViewOpened = onLiveViewOpenedForController;
  actions.onLiveViewOpenFailed = onLiveViewOpenFailedForController;
  actions.isBleConnected = bleStillConnectedForWifi;
  actions.consumePowerOffNotification = consumeCameraPowerOffNotification;
  actions.consumePowerOffDisconnect = consumeCameraPowerOffDisconnect;
  actions.disconnectWifiLiveViewToBleReady = disconnectWifiLiveViewToBleReady;
  actions.disconnectAllTransportsToBleScan = disconnectAllTransportsToBleScan;
  actions.cameraRecoveryInProgress = cameraRecoveryInProgressForController;
  actions.setCameraRecoveryInProgress = setCameraRecoveryInProgressForController;
  actions.reasonRequiresBleRescan = reasonRequiresBleRescan;
  actions.showRecoveryBleReadyRetry = showRecoveryBleReadyRetryForController;
  actions.showRecoveryBleScan = showRecoveryBleScanForController;
  actions.resetBleStackBeforeScanAfterLinkLoss = resetBleStackBeforeScanAfterLinkLoss;
  actions.shortRecoveryDelay = shortRecoveryDelayForController;
  actions.onRecoveryGuardBlocked = onRecoveryGuardBlockedForController;
  actions.onRecoveryFinished = onRecoveryFinishedForController;
  actions.requestManualCameraWake = requestManualCameraWakeForController;
  actions.shutterReady = shutterReadyForController;
  actions.showShutterBleNotReady = showShutterBleNotReadyForController;
  actions.shootAutofocus = shootAutofocusForController;
  actions.onShutterOk = onShutterOkForController;
  actions.onShutterFailed = onShutterFailedForController;
  actions.previewKeptAfterShutterFailure = previewKeptAfterShutterFailureForController;
  actions.showCameraSleepGuardStatus = showCameraSleepGuardStatusForController;
  actions.previewStreamRunning = previewStreamRunningForController;
  actions.readAndProcessLiveViewFrame = readAndProcessLiveViewFrameForController;
  actions.logPreviewStats = logPreviewStatsForController;
  actions.delayAndYield = delayAndYield;
  actions.lastFrameAt = lastFrameAtForController;
  actions.lastLiveViewActivityAt = lastLiveViewActivityAtForController;
  actions.lastCameraRecoveryAt = lastCameraRecoveryAtForController;
  actions.setLastCameraRecoveryAt = setLastCameraRecoveryAtForController;
  actions.liveviewEnabled = liveviewEnabled;
  actions.bleShutterOnlyMode = (RICOH_BLE_SHUTTER_ONLY_MODE != 0);
  actions.wifiOpenAttempts = WIFI_OPEN_ATTEMPTS;
  actions.retryDelayMs = BLE_CONNECT_RETRY_DELAY_MS;
  actions.bleScanRetryIntervalMs = BLE_SCAN_RETRY_INTERVAL_MS;
  actions.liveViewStallTimeoutMs = LIVEVIEW_STALL_TIMEOUT_MS;
  return actions;
}

void refreshPropsIfDue(bool force = false) {
  const uint32_t now = millis();
  if (!force && cameraProps.ok && (now - lastPropsAt) < PROPS_REFRESH_INTERVAL_MS) {
    return;
  }
  if (!grWifi.isConnected()) {
    return;
  }

  CameraProps nextProps;
  if (wifiPreview.fetchProps(nextProps, PROPS_TIMEOUT_MS).ok()) {
    cameraProps = nextProps;
    lastPropsAt = now;
  } else if (force) {
    Serial.printf("HTTP: props refresh failed: %s\n", wifiPreview.lastError().c_str());
  }
}

void updatePreviewOrientation() {
  if (!imuAvailable) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastOrientationPollAt < PREVIEW_ORIENTATION_POLL_MS) {
    return;
  }
  lastOrientationPollAt = now;

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  const uint32_t readStartUs = micros();
  const bool readOk = M5.Imu.getAccel(&ax, &ay, &az);
  const uint32_t readUs = micros() - readStartUs;

  // The accel shares the internal I2C bus with the M5PM1 power chip; a slow read
  // means the two are contending and this poll is stealing time from the
  // LiveView loop. Treat slow or failed reads as problems and disable the
  // feature after too many so the preview is never degraded (see config.h).
  if (!readOk || readUs > IMU_SLOW_READ_US) {
    if (orientationReadProblems < 0xFF) {
      ++orientationReadProblems;
    }
    if (orientationReadProblems >= IMU_MAX_READ_PROBLEMS) {
      imuAvailable = false;
      Serial.printf("IMU: orientation polling disabled after %u slow/failed reads "
                    "(last read=%luus ok=%d) — I2C contention with power chip\n",
                    static_cast<unsigned>(orientationReadProblems),
                    static_cast<unsigned long>(readUs), readOk ? 1 : 0);
    }
    if (!readOk) {
      return;
    }
  } else if (orientationReadProblems > 0) {
    orientationReadProblems = 0;  // fast, clean read — bus is cooperating again
  }

  // The stick is used as an eye-level viewfinder held upright. In that pose
  // gravity lies along the device Y axis: ay ~= +1.0 held normally and
  // ay ~= -1.0 when the stick is rotated 180 degrees in-plane (measured on a
  // real GR IIIx, 2026-07-09). ax/az stay near zero and are just noise, so the
  // flip decision keys off ay alone. A wide dead band (|ay| < 0.45 g) holds the
  // current orientation while the stick is level or being moved, giving strong
  // hysteresis so the preview never flip-flops mid-motion.
  bool flipped = previewFlipped;
  if (ay < -0.45f) {
    flipped = true;
  } else if (ay > 0.45f) {
    flipped = false;
  } else {
    return;  // ambiguous (level / in motion) — keep current orientation
  }

  if (flipped != previewFlipped) {
    previewFlipped = flipped;
    // Flip at the panel level so the live preview AND the status/boot/error
    // screens rotate together. The decoder stays at 0 to avoid a double flip.
    ui.setFlipped(flipped);
    Serial.printf("IMU: display %s (ax=%.2f ay=%.2f az=%.2f)\n",
                  flipped ? "flipped 180" : "normal", ax, ay, az);
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
  previewFrameBuffer.recordFrame(len);

  // During the post-shutter freeze window, keep consuming frames (so the stall
  // watchdog stays fed) but don't render them: the camera is emitting black
  // exposure frames and we want the last live frame to stay on screen.
  if (previewFreezeUntil != 0) {
    if (static_cast<int32_t>(millis() - previewFreezeUntil) < 0) {
      previewFreezeSkipped++;
      return;
    }
    Serial.printf("Preview: capture freeze released, skipped %lu frames\n",
                  static_cast<unsigned long>(previewFreezeSkipped));
    previewFreezeSkipped = 0;
    previewFreezeUntil = 0;
  }

  const uint32_t renderStartMs = millis();
  if (!decoder.drawFrame(ui.getCanvas(), data, len)) {
    Serial.printf("JPEG decode failed len=%u err=%s\n", static_cast<unsigned>(len), decoder.lastError().c_str());
    wifiPreview.recordRenderedFrame(decoder.lastDecodeMs(), millis() - renderStartMs);
  } else {
    if (DRAW_LIVE_OVERLAY) {
      ui.drawOverlay(grWifi.statusText(),
                     wifiPreview.isPreviewRunning() ? "LIVE" : "IDLE",
                     cameraProps.model,
                     cameraProps.battery,
                     currentFps,
                     grWifi.rssi(),
                     decodedFrames,
                     mjpeg.droppedFrames());
    }
    ui.pushCanvas();
    wifiPreview.recordRenderedFrame(decoder.lastDecodeMs(), millis() - renderStartMs);
  }
  lastFrameAt = millis();
}

void resetBlePairingFromKey2() {
  static bool resettingPairing = false;
  if (resettingPairing) {
    return;
  }
  resettingPairing = true;
  cameraRecoveryInProgress = true;

  Serial.println("BLE pairing reset: Button B / KEY2 long press");
  showStatusIfChanged("Reset pairing", "Clearing BLE...", "", "", true);

  closeLiveView("Reset pairing");
  wifiPreview.disconnectWifi();
  bleCamera.disconnect();
  clearCameraSleepGuard("Reset pairing");

  const bool profileCleared = profileStore.clearBlePairing();
  Serial.printf("Profile: BLE pairing keys cleared ok=%d\n", profileCleared ? 1 : 0);

  const String cameraIp = cameraProfile.wifi.cameraIp.length() > 0 ? cameraProfile.wifi.cameraIp : String(GR_HOST);
  cameraProfile = CameraProfile{};
  cameraProfile.wifi.cameraIp = cameraIp;
  wifiPreview.setEndpoint(cameraProfile.wifi.cameraIp.c_str(), GR_PORT);
  pendingFreshWifiCredentials = RicohBleWifiCredentials{};
  wifiCacheRefreshPending = false;
  cameraProps = CameraProps{};
  pendingCameraProps = CameraProps{};
  lastPropsAt = 0;
  liveviewEnabled = true;

  const rvf::Result deleteBondsResult = bleCamera.deleteAllBonds();
  if (deleteBondsResult.failed()) {
    Serial.printf("BLE pairing reset: NimBLE bond delete failed: %s\n", bleCamera.lastError().c_str());
  }
  bleCamera.clearDisconnectReason();
  bleCamera.resetStack(true);

  showStatusIfChanged("Scanning GR BLE", "Pairing mode", "", "", true);
  setCameraFlowState(CameraFlowState::BleScan, "Reset pairing");
  lastFrameAt = millis();
  lastLiveViewActivityAt = lastFrameAt;
  lastCameraRecoveryAt = 0;
  cameraRecoveryInProgress = false;
  resettingPairing = false;
}

void requestManualCameraWake(const char* source, bool fastReconnect) {
  const char* wakeSource = source != nullptr ? source : "manual retry";
  clearCameraSleepGuard(wakeSource);
  liveviewEnabled = true;

  closeLiveView(wakeSource);
  wifiPreview.disconnectWifi();
  bleCamera.disconnect();
  setCameraFlowState(CameraFlowState::BleScan, wakeSource);

  if (fastReconnect) {
    // Auto power-on path: a clean no-security probe just connected/read/disconnected,
    // so the BLE stack is proven healthy. Skip the heavy resetStack() + 3s settle
    // (~4.5s) the manual path needs; wait only briefly for the camera to accept a
    // fresh connection after the probe's disconnect, then connect straight away.
    Serial.printf("BLE: fast auto reconnect after probe (%s)\n", wakeSource);
    showStatusIfChanged("Connecting...", "", preferredBleName(), "", true);
    delay(BLE_AUTO_WAKE_RECONNECT_SETTLE_MS);
    yield();
  } else {
    showStatusIfChanged("Manual retry", "Resetting BLE...", preferredBleName(), "", true);
    Serial.printf("BLE guard: manual retry BLE stack rebuild (%s)\n", wakeSource);
    bleCamera.resetStack(true);
    delay(BLE_MANUAL_WAKE_REINIT_SETTLE_MS);
    yield();
    // NB: avoid the word "Checking camera" here -- that keyword is reserved for the
    // boot/first-probe transient in display.cpp. This is an active connect attempt.
    showStatusIfChanged("Manual retry", "Connecting...", preferredBleName(), "", true);
  }

  // This wake was triggered by the user (BtnA) or by the auto-probe after it
  // already confirmed CAPTURE, so the camera is definitely on. Bypass the
  // passive advert power-bit gate in runBleDiscoveryAtBoot for this one flow so
  // a stale standby-beacon reading can't bounce us straight back into the guard.
  bleSkipAwakeGateOnce = true;
  const bool online = appController.runCameraFlowOnce(makeAppFlowActions(), millis());
  lastFrameAt = millis();
  lastCameraRecoveryAt = online ? millis() : 0;
}

#if RICOH_BLE_AUTO_PROBE
uint32_t lastAutoAwakeScanAt = 0;
// Consecutive no-security probes that read operation-mode CAPTURE in a row.
// Reset to 0 by any non-confirming probe (still asleep/still settling/read
// failed). See RICOH_BLE_AUTO_PROBE_CAPTURE_CONFIRMATIONS in config.h for why
// a single reading is not trusted.
uint8_t consecutiveCaptureProbeConfirmations = 0;

// P2 hybrid auto power-on detection. Runs only while idling in the user-triggered
// camera-off wait state. It LISTENS (observe-only, no connect) for the camera's
// advertising power bit; when the user powers the camera on, the bit flips to
// AWAKE (P0-A). On a debounced awake signal it fires ONE read-only no-security
// probe to confirm CAPTURE, then hands off to the normal connect flow. Nothing
// here ever writes WLAN-ON, so a sleeping camera can never be woken/extended --
// this only detects a power-on the user performed. BtnA remains a manual override.
void serviceAutoProbeIfDue() {
  if (bleCamera.isConnected()) {
    return;
  }
  if (!cameraSleepGuardActive()) {
    return;  // only while idling in the camera-off wait state
  }
  if (cameraPowerProbeBackoffActive()) {
    return;  // honor post-power-off cooldown (ignore dying-gasp adverts)
  }
  if (cameraProfile.bleAddress.length() == 0) {
    return;  // no saved identity to listen for; BtnA manual wake still works
  }

  const uint32_t now = millis();
  if (lastAutoAwakeScanAt != 0 && (now - lastAutoAwakeScanAt) < RICOH_BLE_AUTO_PROBE_INTERVAL_MS) {
    return;
  }
  lastAutoAwakeScanAt = now;

  RicohBleDeviceInfo info;
  info.found = true;
  info.address = cameraProfile.bleAddress;
  info.addressType = cameraProfile.bleAddressType;
  info.name = preferredBleName();

  int powerBit = -1;
  const bool awake = bleCamera.waitForCameraAwake(info, RICOH_BLE_AWAKE_SCAN_WINDOW_MS, &powerBit);
  // First scan completed: we now know the camera's state, so let the idle status
  // settle from the "Checking camera" transient onto "Camera off".
  if (!firstAwakeScanDone) {
    firstAwakeScanDone = true;
    showCameraSleepGuardStatus(true);
  }
  if (!awake) {
    return;  // still asleep / not advertising awake -- keep idling, zero churn
  }

  Serial.printf("AUTO: camera advertising AWAKE (bit=0x%02X) -> confirming via no-security probe\n",
                powerBit >= 0 ? static_cast<unsigned>(powerBit) : 0);
  RicohCameraOperationMode mode = RicohCameraOperationMode::Unknown;
  const bool probed = bleCamera.probeOperationModeNoSecurity(info, mode);
  if (!probed || mode != RicohCameraOperationMode::Capture) {
    // Any non-CAPTURE (or failed) read breaks the streak. This is what filters
    // the transient misread right after power-off, while the camera's BLE MCU
    // is still settling and the advert bit is still lying about being awake.
    consecutiveCaptureProbeConfirmations = 0;
    Serial.printf("AUTO: probe did not confirm CAPTURE (probed=%d mode=%d) -- keep waiting\n",
                  probed ? 1 : 0,
                  static_cast<int>(mode));
    return;
  }

  ++consecutiveCaptureProbeConfirmations;
  if (consecutiveCaptureProbeConfirmations < RICOH_BLE_AUTO_PROBE_CAPTURE_CONFIRMATIONS) {
    Serial.printf("AUTO: CAPTURE probe %u/%u -- waiting for a repeat confirmation before waking "
                  "(guards against power-off settling noise)\n",
                  static_cast<unsigned>(consecutiveCaptureProbeConfirmations),
                  static_cast<unsigned>(RICOH_BLE_AUTO_PROBE_CAPTURE_CONFIRMATIONS));
    return;
  }

  consecutiveCaptureProbeConfirmations = 0;
  Serial.println("AUTO: CAPTURE confirmed -> user powered camera on, starting connect flow");
  requestManualCameraWake("auto power-on detect", /*fastReconnect=*/true);
}
#endif  // RICOH_BLE_AUTO_PROBE

void shutdownStickS3() {
  static bool shuttingDown = false;
  if (shuttingDown) {
    return;
  }
  shuttingDown = true;
  cameraRecoveryInProgress = true;

  Serial.println("Power: shutdown requested");

  // If the remote is still connected to the camera when it powers off, tell the
  // camera to power off too (write 0x00 to the Camera Power characteristic).
  // Must happen while BLE is still up — before bleCamera.disconnect() below.
  if (RICOH_BLE_POWER_OFF_CAMERA_ON_STICK_SHUTDOWN && bleCamera.isConnected()) {
    Serial.println("Power: commanding camera power-off over BLE");
    const rvf::Result cameraOff = bleCamera.powerOffCamera();
    if (cameraOff.failed()) {
      Serial.printf("Power: camera power-off failed: %s\n", bleCamera.lastError().c_str());
    } else {
      Serial.println("Power: camera power-off command sent");
    }
    Serial.flush();
    // Give the camera time to act on the BLE command while the link is still up.
    // Tearing BLE down immediately can abort processing on the camera side.
    delay(600);
  }

  closeLiveView("power off");
  wifiPreview.disconnectWifi();
  bleCamera.disconnect();

  ui.showBoot("Release PWR...");
  const uint32_t startMs = millis();
  while (isStickPowerButtonPressed() && (millis() - startMs) < POWER_BUTTON_RELEASE_WAIT_MS) {
    M5.update();
    delay(20);
    yield();
  }

  ui.showBoot("Power off...");
  delay(200);
  if (stickPowerReady) {
    Serial.println("Power: M5PM1 shutdown command");
    Serial.flush();
    const m5pm1_err_t err = stickPower.shutdown();
    if (err != M5PM1_OK) {
      Serial.printf("Power: M5PM1 shutdown failed err=%d; fallback to M5Unified\n", static_cast<int>(err));
      Serial.flush();
    }
    delay(300);
  }
  M5.Power.powerOff();
  while (true) {
    delay(1000);
  }
}

void handleButtons() {
  const ButtonEvents events = buttons.poll();
  const rvf::UserCommand command = rvf::ButtonInput::commandFromEvents(events);

  if (command == rvf::UserCommand::PowerOff || pollStickPowerHold()) {
    shutdownStickS3();
    return;
  }

  if (command == rvf::UserCommand::ResetPairing) {
    resetBlePairingFromKey2();
    return;
  }

  if (command == rvf::UserCommand::Shoot) {
    appController.handleUserCommand(makeAppFlowActions(), command);
  }
}

const char* appEventTypeName(rvf::AppEventType type) {
  switch (type) {
    case rvf::AppEventType::None: return "None";
    case rvf::AppEventType::BootCompleted: return "BootCompleted";
    case rvf::AppEventType::BleScanStarted: return "BleScanStarted";
    case rvf::AppEventType::BleDeviceFound: return "BleDeviceFound";
    case rvf::AppEventType::BleConnected: return "BleConnected";
    case rvf::AppEventType::BleDisconnected: return "BleDisconnected";
    case rvf::AppEventType::CameraPowerOn: return "CameraPowerOn";
    case rvf::AppEventType::CameraPowerOff: return "CameraPowerOff";
    case rvf::AppEventType::CameraPowerUnknown: return "CameraPowerUnknown";
    case rvf::AppEventType::WifiActivationRequested: return "WifiActivationRequested";
    case rvf::AppEventType::WifiConnected: return "WifiConnected";
    case rvf::AppEventType::WifiDisconnected: return "WifiDisconnected";
    case rvf::AppEventType::HttpProbeSucceeded: return "HttpProbeSucceeded";
    case rvf::AppEventType::HttpProbeFailed: return "HttpProbeFailed";
    case rvf::AppEventType::PreviewStarted: return "PreviewStarted";
    case rvf::AppEventType::PreviewStopped: return "PreviewStopped";
    case rvf::AppEventType::PreviewTimeout: return "PreviewTimeout";
    case rvf::AppEventType::ButtonShortPress: return "ButtonShortPress";
    case rvf::AppEventType::ButtonLongPress: return "ButtonLongPress";
    case rvf::AppEventType::ShutterPressed: return "ShutterPressed";
    case rvf::AppEventType::ErrorRaised: return "ErrorRaised";
  }
  return "Unknown";
}

rvf::SystemHealthSnapshot makeSystemHealthSnapshot() {
  rvf::SystemHealthSnapshot snapshot;
  snapshot.appState = appController.state();
  snapshot.bleConnected = bleCamera.isConnected();
  snapshot.wifiConnected = grWifi.isConnected();
  snapshot.previewRunning = wifiPreview.isPreviewRunning();
  snapshot.liveviewEnabled = liveviewEnabled;
  snapshot.cameraSleepGuardActive = cameraSleepGuardActive();
  snapshot.cameraRecoveryInProgress = cameraRecoveryInProgress;
  snapshot.lastFrameAt = lastFrameAt;
  snapshot.lastLiveViewActivityAt = lastLiveViewActivityAt;
  snapshot.liveViewStallTimeoutMs = LIVEVIEW_STALL_TIMEOUT_MS;
  return snapshot;
}

void serviceSystemSupervisor(uint32_t nowMs) {
  rvf::AppMessage message;
  if (!systemSupervisor.check(nowMs, makeSystemHealthSnapshot(), message)) {
    return;
  }

  Serial.printf("Supervisor: event=%s state=%s code=%d detail=%s\n",
                appEventTypeName(message.type),
                rvf::appStateName(appController.state()),
                message.code,
                message.detail != nullptr ? message.detail : "");
}

void updateStatusUiIfDue() {
  const uint32_t now = millis();
  if ((now - lastStatusAt) < UI_STATUS_INTERVAL_MS) {
    return;
  }
  lastStatusAt = now;

  if (cameraSleepGuardActive()) {
    showCameraSleepGuardStatus(false);
    return;
  }

#if RICOH_BLE_SHUTTER_ONLY_MODE
  // BLE-only remote shutter: report the BLE link and shutter hint instead of
  // Wi-Fi state, since Wi-Fi/LiveView is intentionally never brought up.
  if (bleCamera.isConnected()) {
    showStatusIfChanged("BLE_READY",
                        "BtnA: shutter",
                        cameraProfile.cameraName.length() ? cameraProfile.cameraName
                                                          : String("RICOH GR"),
                        "");
  } else {
    showStatusIfChanged("BLE SEARCHING", "Press BtnA to reconnect", "", "");
  }
  return;
#else
  if (!wifiPreview.isPreviewRunning()) {
    showStatusIfChanged(grWifi.statusText(),
                        liveviewEnabled ? grWifi.localIPString() : "Preview paused",
                        cameraProps.model,
                        cameraProps.battery);
  }
#endif
}

void runAppTick() {
  const uint32_t now = millis();
  const rvf::AppTickPlan tickPlan = appController.planTick(now);

  updatePreviewOrientation();

  if (tickPlan.handleButtons) {
    handleButtons();
  }
#if RICOH_BLE_AUTO_PROBE
  // Passive auto power-on detection while idling in the camera-off wait state.
  // No-op (returns immediately) once connected or outside that state.
  serviceAutoProbeIfDue();
#endif
  const uint32_t controllerNow = millis();
  const rvf::AppFlowActions actions = makeAppFlowActions();
  if (tickPlan.serviceCameraFlow) {
    appController.serviceCameraFlowIfNeeded(actions, controllerNow);
  }
  if (tickPlan.monitorWifi) {
    appController.monitorWifi(actions);
  }
  if (tickPlan.refreshProps) {
    refreshPropsIfDue();
  }
  if (tickPlan.monitorLiveView) {
    appController.monitorLiveView(actions, controllerNow);
  }
  serviceSystemSupervisor(millis());
  if (tickPlan.refreshWifiCache) {
    refreshWifiCacheIfDue();
  }
  if (tickPlan.updateStatusUi) {
    updateStatusUiIfDue();
  }
  delay(1);
}

}  // namespace

void setup() {
  Serial.begin(rvf::AppConfig::SerialPort::kBaud);
  appController.begin(CameraFlowState::BleScan);
  systemSupervisor.begin(millis());

  ui.begin();
  ui.showBoot(rvf::AppConfig::Ui::kBootMessage);
  waitForSerialConsole();

  beginStickPower();
  buttons.begin();
  ricohBle.setServiceCallback(serviceButtonsDuringBleOperation);
  ricohBle.setPasskeyEntryProvider(providePairingPasskey);
  decoder.begin();
  grWifi.begin();

  imuAvailable = M5.Imu.isEnabled();
  Serial.printf("IMU: %s\n", imuAvailable ? "enabled (preview auto-rotates)" : "not detected (rotation off)");

  applyDefaultProfile();

  if (!psramFound()) {
    LOGLINE_W("MEM", "PSRAM not found; JPEG buffer allocation may fail");
    ui.showError("PSRAM not found");
  }

  rvf::PreviewFrameBufferStorage frameBufferStorage = rvf::PreviewFrameBufferStorage::Psram;
  frameBuffer = static_cast<uint8_t*>(heap_caps_malloc(rvf::AppConfig::Buffer::kFrameBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (frameBuffer == nullptr) {
    frameBufferStorage = rvf::PreviewFrameBufferStorage::InternalRam;
    frameBuffer = static_cast<uint8_t*>(heap_caps_malloc(rvf::AppConfig::Buffer::kFrameBufferSize, MALLOC_CAP_8BIT));
  }
  if (frameBuffer == nullptr) {
    LOGLINE_E("MEM", "Failed to allocate JPEG frame buffer");
    ui.showError("Frame buffer alloc failed");
    while (true) {
      delay(1000);
    }
  }

  if (!previewFrameBuffer.attach(frameBuffer, rvf::AppConfig::Buffer::kFrameBufferSize, frameBufferStorage)) {
    LOGLINE_E("FRAME", "Failed to attach JPEG frame buffer");
    ui.showError("Frame buffer attach failed");
    while (true) {
      delay(1000);
    }
  }
  mjpeg.begin(previewFrameBuffer.data(), previewFrameBuffer.capacity(), onJpegFrame, nullptr);

  // Do NOT auto-connect at boot. The GR IIIx cannot be safely auto-woken
  // (WLAN-ON extends the lens), so the stick must never initiate a link to a
  // possibly-sleeping camera. Instead we idle in the camera-off wait state and
  // detect the user powering the camera on PASSIVELY: serviceAutoProbeIfDue()
  // listens for the advertising AWAKE power bit (RICOH_BLE_AUTO_PROBE), and only
  // once a read-only probe confirms CAPTURE does it run the full connect ->
  // Wi-Fi -> LiveView flow. BtnA (requestManualCameraWake) remains a manual
  // override. Nothing here or in the auto-probe ever writes to a sleeping camera.
  setupCameraFlowActive = false;
  cameraAutoWakeBlocked = true;
  cameraPowerState = RicohCameraPowerState::OffOrShuttingDown;
  cameraOperationMode = RicohCameraOperationMode::Unknown;
  setCameraFlowState(CameraFlowState::CameraPowerOff, "boot: wait for user power-on");
  Serial.println("Boot: idle; auto-detecting camera power-on via advertising (BtnA = manual override)");
  showCameraSleepGuardStatus(true);
  lastCameraRecoveryAt = 0;
  lastFrameAt = millis();
  lastLiveViewActivityAt = lastFrameAt;
  lastStatusAt = 0;
}

void loop() {
  runAppTick();
}
