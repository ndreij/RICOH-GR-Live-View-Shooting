#pragma once

#include <Arduino.h>

struct RicohBleDeviceInfo {
  bool found = false;
  String name;
  String address;
  uint8_t addressType = 0;
  int rssi = 0;
  bool connectable = false;
  bool hasInfoService = false;
  bool hasCameraService = false;
  bool hasShootingService = false;
  bool hasControlService = false;
};

struct RicohBleWifiCredentials {
  bool valid = false;
  bool encryptedPassphrase = false;
  int securityType = -1;
  uint16_t frequencyMhz = 0;
  uint8_t channel = 0;
  String ssid;
  String passphrase;
  String bssid;
};

struct RicohBleConnectOptions {
  uint32_t timeoutMs = 0;
  uint32_t securityWaitMs = 0;
  uint32_t preConnectDelayMs = 0;
  bool exchangeMtu = true;
  // P0-B (Fable review): when true, keep any cached GATT attribute table across
  // reconnects (NimBLE connect deleteAttributes=false) so we skip full service
  // re-discovery on every probe.
  bool reuseAttributes = false;
  // P0-B: when true, do NOT initiate bonding/encryption after link-up. Used by
  // the lightweight power-state probe: we only need a read of the operation-mode
  // characteristic. Initiating security on a camera whose BLE MCU is mid-restart
  // is what produces rc=524 (Command Disallowed) and the self-inflicted
  // reason-534 (local-host) teardown during fast reconnect churn.
  bool skipSecurity = false;
};

enum class RicohCameraPowerState {
  Unknown,
  On,
  OffOrShuttingDown,
};

enum class RicohCameraOperationMode {
  Unknown,
  Capture,
  Playback,
  BleStartup,
  Other,
  PowerOffTransfer,
};

class RicohBleClient {
public:
  using ServiceCallback = bool (*)();
  // On-device passkey entry provider. Called repeatedly during BLE security
  // negotiation while the camera is displaying its random 6-digit code.
  // firstCall is true only on the first invocation of a given entry session
  // (reset UI/state). Return true once the user has committed all 6 digits and
  // write the code to outCode; return false to keep waiting for input.
  using PasskeyEntryProvider = bool (*)(bool firstCall, uint32_t& outCode);

  void begin();
  void setServiceCallback(ServiceCallback callback);
  void setPasskeyEntryProvider(PasskeyEntryProvider provider);
  RicohBleDeviceInfo scanForCamera(const String& preferredAddress, const String& preferredName, uint32_t scanSeconds);
  bool connect(const RicohBleDeviceInfo& info, uint32_t timeoutMs);
  bool connect(const RicohBleDeviceInfo& info, const RicohBleConnectOptions& options);
  bool isBonded(const RicohBleDeviceInfo& info);
  bool isConnected() const;
  bool shutterReady() const;
  bool shoot(bool autofocus = true);
  // Resolve and cache the shooting-control characteristics up front so the very
  // first shutter press after connecting does not pay the one-off GATT service/
  // characteristic discovery cost (which otherwise stalls the first shot by
  // several seconds). Best-effort and idempotent; called after a good connect.
  void prewarmShutter();
  bool openWifi();
  // Command the camera to power off by writing 0x00 (Off) to the Camera Power
  // characteristic (RICOH_BLE_POWER_STATE_HANDLE). Verified writable on the GR
  // IIIx (GATT dump: r=1 w=1 n=1) and documented Write/Mandatory in the Ricoh
  // GR BLE API (values 0=Off / 1=On / 2=Sleep). Best effort: returns false and
  // sets lastError() if not connected, the handle is unconfigured, or the write
  // fails. Intended to be called while BLE is still up, just before shutdown.
  bool powerOffCamera();
  bool readPowerState(RicohCameraPowerState& state);
  bool readOperationMode(RicohCameraOperationMode& mode);
  // P0-B lightweight power-state probe (Fable review): direct-connect by address
  // with NO security initiation and no forced re-discovery, read operation mode,
  // clean disconnect. Times each phase and logs it. This is the decisive test of
  // whether operation-mode is readable pre-encryption -- if so, wake detection
  // and manual connect can both skip the heavy, failure-prone security step.
  // Returns true iff the operation-mode read succeeded; `mode` holds the result.
  bool probeOperationModeNoSecurity(const RicohBleDeviceInfo& info, RicohCameraOperationMode& mode);
  // P2 passive awake detector: run a bounded observe-only scan and return true as
  // soon as the address-matched camera advertises the AWAKE power bit
  // (manufacturer-data last byte == RICOH_BLE_ADV_POWER_BIT_AWAKE) on
  // RICOH_BLE_AWAKE_ADVERT_DEBOUNCE consecutive matched reports. NEVER connects
  // and NEVER writes anything, so it cannot wake/extend the lens of a sleeping
  // camera -- it only detects that the user already powered it on. Returns false
  // on timeout (still asleep) or scan error. `lastPowerBitOut`, when non-null,
  // receives the most recent observed power bit (-1 if no matched advert was
  // seen) for diagnostics.
  bool waitForCameraAwake(const RicohBleDeviceInfo& info, uint32_t timeoutMs, int* lastPowerBitOut = nullptr);
  bool enablePowerStateNotify();
  bool consumePowerOffNotification();
  bool waitForWifiCredentials(RicohBleWifiCredentials& credentials, uint32_t timeoutMs);
  void disconnect();
  int consumeDisconnectReason();
  void clearDisconnectReason();
  bool deleteAllBonds();
  void resetStack(bool clearObjects = false);
  bool lastFailureWasResourceExhausted() const;

  String statusText() const;
  const String& lastError() const;

private:
  // Populates _shootingFlavorChar / _operationRequestChar (cached across the
  // connection). Returns true if both are resolved and writable. No-op if the
  // cache is already valid. Pointers are owned by the NimBLE client and are
  // invalidated in disconnect().
  bool ensureShootingCharacteristics(String& errorOut);

  bool _begun = false;
  bool _connected = false;
  bool _lastFailureResourceExhausted = false;
  String _lastError;
  void* _client = nullptr;
  void* _shootingFlavorChar = nullptr;    // NimBLERemoteCharacteristic* (cached)
  void* _operationRequestChar = nullptr;  // NimBLERemoteCharacteristic* (cached)
};
