#pragma once

#include <Arduino.h>

#ifndef GR_HOST
#define GR_HOST "192.168.0.1"
#endif

#ifndef GR_PORT
#define GR_PORT 80
#endif

constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t DISPLAY_HEIGHT = 135;

constexpr size_t FRAME_BUFFER_SIZE = 256 * 1024;
// Bytes pulled per WiFiClient::read() call while draining the LiveView MJPEG
// stream. Larger values mean fewer read()/process() round trips per JPEG
// frame (typical GR LiveView frames are well over 2KB), at the cost of a
// bigger on-stack buffer. Raised from 2048 as part of the live-view refresh
// rate investigation (2026-07-06); re-check WifiPreviewService's
// "read=...ms" stat after this change to confirm it actually helped on real
// hardware before tuning further.
constexpr size_t STREAM_READ_BUFFER_SIZE = 8192;

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_CHANNEL_HINT_CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t WIFI_CACHED_CONNECT_TIMEOUT_MS = 1200;
constexpr uint32_t WIFI_CACHED_CONNECT_GRACE_MS = 700;
constexpr uint32_t WIFI_CACHE_REFRESH_DELAY_MS = 5000;
constexpr uint32_t BLE_SCAN_RETRY_INTERVAL_MS = 1000;
constexpr uint32_t PROPS_TIMEOUT_MS = 3500;
constexpr uint32_t LIVEVIEW_STALL_TIMEOUT_MS = 5000;
constexpr uint32_t UI_STATUS_INTERVAL_MS = 1000;
constexpr uint32_t POWER_BUTTON_POLL_MS = 50;
constexpr uint32_t POWER_BUTTON_HOLD_MS = 1200;
constexpr uint32_t POWER_BUTTON_RELEASE_WAIT_MS = 3000;
constexpr uint8_t KEY2_FALLBACK_GPIO = 12;
constexpr uint32_t KEY2_PAIRING_RESET_HOLD_MS = 3000;
// On-device BLE passkey entry: BtnA held at least this long locks the current
// digit and advances to the next; a shorter tap increments the digit.
constexpr uint32_t PASSKEY_ADVANCE_HOLD_MS = 600;
constexpr uint32_t PROPS_REFRESH_INTERVAL_MS = 60000;

// Decode-time downscale applied before drawing to the 240x135 panel.
// JPEG_SCALE_HALF is the current default (halves decode/render cost vs full
// res). If LiveView still feels slow after checking WifiPreviewService's
// decode_ms/render_ms stats, try JPEG_SCALE_QUARTER for a bigger speed win
// at the cost of visible detail. Override from platformio.ini without
// editing this file, e.g.:
//   build_flags = ${env:m5stack-sticks3.build_flags} -DJPEG_SCALE_POLICY=JPEG_SCALE_QUARTER
#ifndef JPEG_SCALE_POLICY
#define JPEG_SCALE_POLICY JPEG_SCALE_HALF
#endif

constexpr uint32_t BLE_SCAN_SECONDS = 2;
constexpr uint32_t BLE_FAST_CONNECT_TIMEOUT_MS = 3000;
constexpr uint32_t BLE_CONNECT_TIMEOUT_MS = 8000;
constexpr uint8_t BLE_CONNECT_ATTEMPTS = 12;
constexpr uint32_t BLE_CONNECT_RETRY_DELAY_MS = 1000;
constexpr uint32_t BLE_SCAN_TO_CONNECT_DELAY_MS = 500;
constexpr uint8_t BLE_STACK_RESET_AFTER_FAILURES = 2;
constexpr uint32_t BLE_STACK_RESET_DELAY_MS = 1500;
constexpr uint32_t BLE_RECOVERY_STACK_RESET_GRACE_MS = 700;
constexpr uint32_t BLE_DISCONNECT_WAIT_MS = 1200;
constexpr uint32_t RICOH_BLE_BONDED_SECURITY_WAIT_MS = 1500;
// Long enough for on-device (button) passkey entry. Note the GR IIIx itself
// times out its pairing prompt around 30s, so the effective ceiling is the
// camera's; the wait loop also exits early on disconnect. GR IV pairs via
// numeric-comparison auto-confirm and never touches this budget.
constexpr uint32_t RICOH_BLE_SECURITY_WAIT_MS = 45000;
constexpr uint8_t FIRST_BOOT_BLE_PAIRING_ATTEMPTS = 12;
constexpr uint32_t SERIAL_BOOT_WAIT_MS = 500;
constexpr uint32_t CAMERA_POWER_OFF_COOLDOWN_MS = 15000;
constexpr uint32_t CAMERA_POWER_OFF_PROBE_BACKOFF_MS = 8000;
constexpr uint32_t BLE_MANUAL_WAKE_REINIT_SETTLE_MS = 3000;
constexpr int RICOH_BLE_DISCONNECT_REMOTE_USER = 0x213;
constexpr int RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF = 0x215;
constexpr uint8_t RICOH_BLE_POWER_READ_RETRIES = 2;
constexpr uint8_t RICOH_BLE_OPERATION_MODE_READ_RETRIES = 2;
constexpr uint32_t RICOH_BLE_POWER_NOTIFY_SETTLE_MS = 30;
constexpr bool RICOH_BLE_REQUIRE_POWER_ON_BEFORE_WIFI = true;
constexpr bool RICOH_BLE_ALLOW_WIFI_WHEN_POWER_UNKNOWN = false;
constexpr bool RICOH_BLE_BLOCK_WIFI_IN_STANDBY_OPERATION_MODE = true;

constexpr bool RICOH_BLE_AUTO_WLAN_ON_BOOT = true;
constexpr uint32_t RICOH_BLE_POST_WLAN_ON_WAIT_MS = 0;
constexpr uint32_t RICOH_BLE_WIFI_CREDENTIAL_WAIT_MS = 10000;
constexpr uint32_t RICOH_BLE_WIFI_CREDENTIAL_POLL_MS = 500;
constexpr uint8_t WIFI_OPEN_ATTEMPTS = 3;

// Verified from the RICOH GR Android app BLE traffic captured on 2026-06-27.
constexpr uint16_t RICOH_BLE_GR4_WLAN_POWER_HANDLE = 0x0135;
constexpr uint8_t RICOH_BLE_GR4_WLAN_ON_VALUE = 0x01;
constexpr uint16_t RICOH_BLE_GR4_WLAN_SSID_HANDLE = 0x0138;
constexpr uint16_t RICOH_BLE_GR4_WLAN_PASSPHRASE_HANDLE = 0x013A;
constexpr uint16_t RICOH_BLE_GR4_WLAN_SECURITY_HANDLE = 0x013C;
constexpr uint16_t RICOH_BLE_GR4_WLAN_FREQUENCY_HANDLE = 0x013E;
constexpr uint16_t RICOH_BLE_GR4_WLAN_BSSID_HANDLE = 0x0140;

// Verified from Android HCI logs captured on 2026-06-28.
// 0x01: camera powered on / controllable, 0x00: power-off or shutting down.
constexpr uint16_t RICOH_BLE_GR4_POWER_STATE_HANDLE = 0x00EB;
constexpr uint16_t RICOH_BLE_GR4_POWER_STATE_CCCD_HANDLE = 0x00EC;
constexpr uint8_t RICOH_BLE_GR4_POWER_STATE_ON_VALUE = 0x01;
constexpr uint8_t RICOH_BLE_GR4_POWER_STATE_OFF_VALUE = 0x00;

// GR IIIx GATT handles — verified 2026-07-08 from an on-device GATT dump of a
// real "RICOH GR IIIx" (RICOH_BLE_GATT_DUMP_ON_CONNECT), cross-referenced with
// the characteristic UUIDs documented by dm-zharov/ricoh-gr-bluetooth-api.
//
// IMPORTANT: the GR IIIx GATT layout is fundamentally different from the GR IV.
// It has NO 0x0135-range "WLAN power/security/frequency/BSSID" characteristics.
// Instead the WLAN service is F37F568F-9071-445D-A938-5441F2E82399 exposing only
// Network Type / SSID / Passphrase / Channel:
//   Network Type 9111CDD0-...  -> handle 0x00F0 (r/w/n)
//   SSID         90638E5A-...  -> handle 0x00F3 (r/w)
//   Passphrase   0F38279C-...  -> handle 0x00F5 (r/w)
//   Channel      51DE6EBC-...  -> handle 0x00F7 (r/w)
// Camera Power is characteristic B58CE84C-0666-4DE9-BEC8-2D27B27B3211 in the
// Camera service (4B445988-...), at handle 0x00BC (value) / 0x00BD (CCCD) — NOT
// GR IV's 0x00EB.
//
// AP-enable: unlike the GR IV (which has a dedicated 0x0135 "WLAN power" toggle),
// the GR IIIx brings up its Wi-Fi AP by writing the Network Type characteristic
// (9111CDD0-..., handle 0x00F0, sint8): 0x01 = AP mode ON, 0x00 = OFF. Source:
// dm-zharov/ricoh-gr-bluetooth-api wlan_control_command/network_type.md. SSID and
// passphrase (0x00F3 / 0x00F5) are static r/w config values readable at any time,
// so credentials can be read before or after the AP is enabled. We therefore
// repurpose RICOH_BLE_GR3X_WLAN_POWER_HANDLE as the Network Type handle: openWifi()
// writes RICOH_BLE_GR3X_WLAN_ON_VALUE (0x01) to it to switch the camera into AP mode.
constexpr uint16_t RICOH_BLE_GR3X_WLAN_POWER_HANDLE      = 0x00F0;  // Network Type 9111CDD0-...; write 0x01 = AP mode ON
constexpr uint8_t  RICOH_BLE_GR3X_WLAN_ON_VALUE           = 0x01;
constexpr uint16_t RICOH_BLE_GR3X_WLAN_SSID_HANDLE       = 0x00F3;  // SSID   90638E5A-...
constexpr uint16_t RICOH_BLE_GR3X_WLAN_PASSPHRASE_HANDLE = 0x00F5;  // Passphrase 0F38279C-...
constexpr uint16_t RICOH_BLE_GR3X_WLAN_SECURITY_HANDLE   = 0;       // no equivalent (Network Type 0x00F0 has different semantics)
constexpr uint16_t RICOH_BLE_GR3X_WLAN_FREQUENCY_HANDLE  = 0;       // no MHz char (Channel 0x00F7 is a channel index, not frequency)
constexpr uint16_t RICOH_BLE_GR3X_WLAN_BSSID_HANDLE      = 0;       // GR IIIx does not expose a BSSID characteristic
constexpr uint16_t RICOH_BLE_GR3X_POWER_STATE_HANDLE     = 0x00BC;  // Camera Power B58CE84C-...
constexpr uint16_t RICOH_BLE_GR3X_POWER_STATE_CCCD_HANDLE = 0x00BD; // CCCD follows the value handle
constexpr uint8_t  RICOH_BLE_GR3X_POWER_STATE_ON_VALUE   = 0x01;
constexpr uint8_t  RICOH_BLE_GR3X_POWER_STATE_OFF_VALUE  = 0x00;

// BLE-only remote shutter mode: connect over BLE and stay in BLE_READY as a
// pure shutter remote, without ever bringing up Wi-Fi / LiveView. Defaults off
// for both the GR IV and GR IIIx (full LiveView flow) now that the GR IIIx
// Wi-Fi AP-enable path (Network Type 0x00F0) is verified working. Override from
// platformio.ini with -DRICOH_BLE_SHUTTER_ONLY_MODE=1 to build a pure BLE
// remote shutter (see the m5stack-sticks3-gr3x-shutter env).
#ifndef RICOH_BLE_SHUTTER_ONLY_MODE
#  define RICOH_BLE_SHUTTER_ONLY_MODE 0
#endif

// Set to 1 to print all GATT services/characteristics on every BLE connect.
// Essential for discovering handle values on a new camera model (e.g. GR IIIx).
#ifndef RICOH_BLE_GATT_DUMP_ON_CONNECT
#define RICOH_BLE_GATT_DUMP_ON_CONNECT 0
#endif

// Active GATT handle set — selected at compile time by -DCAMERA_MODEL_GR3X.
// GR IV (default) handles are verified; GR IIIx handles are placeholders.
#ifdef CAMERA_MODEL_GR3X
#  define RICOH_BLE_WLAN_POWER_HANDLE       RICOH_BLE_GR3X_WLAN_POWER_HANDLE
#  define RICOH_BLE_WLAN_ON_VALUE           RICOH_BLE_GR3X_WLAN_ON_VALUE
#  define RICOH_BLE_WLAN_SSID_HANDLE        RICOH_BLE_GR3X_WLAN_SSID_HANDLE
#  define RICOH_BLE_WLAN_PASSPHRASE_HANDLE  RICOH_BLE_GR3X_WLAN_PASSPHRASE_HANDLE
#  define RICOH_BLE_WLAN_SECURITY_HANDLE    RICOH_BLE_GR3X_WLAN_SECURITY_HANDLE
#  define RICOH_BLE_WLAN_FREQUENCY_HANDLE   RICOH_BLE_GR3X_WLAN_FREQUENCY_HANDLE
#  define RICOH_BLE_WLAN_BSSID_HANDLE       RICOH_BLE_GR3X_WLAN_BSSID_HANDLE
#  define RICOH_BLE_POWER_STATE_HANDLE      RICOH_BLE_GR3X_POWER_STATE_HANDLE
#  define RICOH_BLE_POWER_STATE_CCCD_HANDLE RICOH_BLE_GR3X_POWER_STATE_CCCD_HANDLE
#  define RICOH_BLE_POWER_STATE_ON_VALUE    RICOH_BLE_GR3X_POWER_STATE_ON_VALUE
#  define RICOH_BLE_POWER_STATE_OFF_VALUE   RICOH_BLE_GR3X_POWER_STATE_OFF_VALUE
#else
#  define RICOH_BLE_WLAN_POWER_HANDLE       RICOH_BLE_GR4_WLAN_POWER_HANDLE
#  define RICOH_BLE_WLAN_ON_VALUE           RICOH_BLE_GR4_WLAN_ON_VALUE
#  define RICOH_BLE_WLAN_SSID_HANDLE        RICOH_BLE_GR4_WLAN_SSID_HANDLE
#  define RICOH_BLE_WLAN_PASSPHRASE_HANDLE  RICOH_BLE_GR4_WLAN_PASSPHRASE_HANDLE
#  define RICOH_BLE_WLAN_SECURITY_HANDLE    RICOH_BLE_GR4_WLAN_SECURITY_HANDLE
#  define RICOH_BLE_WLAN_FREQUENCY_HANDLE   RICOH_BLE_GR4_WLAN_FREQUENCY_HANDLE
#  define RICOH_BLE_WLAN_BSSID_HANDLE       RICOH_BLE_GR4_WLAN_BSSID_HANDLE
#  define RICOH_BLE_POWER_STATE_HANDLE      RICOH_BLE_GR4_POWER_STATE_HANDLE
#  define RICOH_BLE_POWER_STATE_CCCD_HANDLE RICOH_BLE_GR4_POWER_STATE_CCCD_HANDLE
#  define RICOH_BLE_POWER_STATE_ON_VALUE    RICOH_BLE_GR4_POWER_STATE_ON_VALUE
#  define RICOH_BLE_POWER_STATE_OFF_VALUE   RICOH_BLE_GR4_POWER_STATE_OFF_VALUE
#endif


#ifndef RICOH_BLE_INFO_SERVICE_UUID
#define RICOH_BLE_INFO_SERVICE_UUID "9A5ED1C5-74CC-4C50-B5B6-66A48E7CCFF1"
#endif
#ifndef RICOH_BLE_CAMERA_SERVICE_UUID
#define RICOH_BLE_CAMERA_SERVICE_UUID "4B445988-CAA0-4DD3-941D-37B4F52ACA86"
#endif
#ifndef RICOH_BLE_OPERATION_MODE_UUID
#define RICOH_BLE_OPERATION_MODE_UUID "1452335A-EC7F-4877-B8AB-0F72E18BB295"
#endif
#ifndef RICOH_BLE_SHOOTING_SERVICE_UUID
#define RICOH_BLE_SHOOTING_SERVICE_UUID "9F00F387-8345-4BBC-8B92-B87B52E3091A"
#endif
#ifndef RICOH_BLE_SHOOTING_FLAVOR_UUID
#define RICOH_BLE_SHOOTING_FLAVOR_UUID "B29E6DE3-1AEC-48C1-9D05-02CEA57CE664"
#endif
#ifndef RICOH_BLE_OPERATION_REQUEST_UUID
#define RICOH_BLE_OPERATION_REQUEST_UUID "559644B8-E0BC-4011-929B-5CF9199851E7"
#endif
#ifndef RICOH_BLE_CONTROL_SERVICE_UUID
#define RICOH_BLE_CONTROL_SERVICE_UUID "0F291746-0C80-4726-87A7-3C501FD3B4B6"
#endif
