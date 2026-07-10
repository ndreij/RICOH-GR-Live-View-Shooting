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
// Taking a photo briefly interrupts the camera's HTTP LiveView stream. Tolerate
// transient read errors for this long (holding the last frame on screen instead
// of tearing the stream down) before giving up and triggering reconnect. Kept
// below LIVEVIEW_STALL_TIMEOUT_MS so a genuine stream break still recovers.
constexpr uint32_t LIVEVIEW_READ_ERROR_GRACE_MS = 2000;
// When the shutter fires, the camera blanks its LiveView for the exposure: it
// emits near-black frames and stalls the stream. Freeze the last good frame on
// screen for this long after a shot, then let the stream through again.
// Deliberately shorter than the full blank period: we want the last couple of
// the camera's own black exposure frames to show right at the end of the
// freeze, as a brief "shutter fired" flash, instead of the freeze fully
// papering over the blackout and giving zero visual feedback that a shot was
// taken. Tune to taste on real hardware -- too short and the flash never
// shows before the live feed resumes; too long and it's back to no feedback.
constexpr uint32_t PREVIEW_CAPTURE_FREEZE_MS = 1000;
constexpr uint32_t UI_STATUS_INTERVAL_MS = 1000;
constexpr uint32_t POWER_BUTTON_POLL_MS = 50;
constexpr uint32_t POWER_BUTTON_HOLD_MS = 1200;
constexpr uint32_t POWER_BUTTON_RELEASE_WAIT_MS = 3000;
constexpr uint8_t KEY2_FALLBACK_GPIO = 12;
constexpr uint32_t KEY2_PAIRING_RESET_HOLD_MS = 3000;
// Front button (BtnA) alternative to the KEY2 long-press above: holding BtnA
// this long also clears BLE pairing and starts a fresh pairing scan. Same
// duration as KEY2_PAIRING_RESET_HOLD_MS so both gestures feel identical.
constexpr uint32_t BTNA_PAIRING_HOLD_MS = 3000;
// On-device BLE passkey entry: BtnA held at least this long locks the current
// digit and advances to the next; a shorter tap increments the digit.
constexpr uint32_t PASSKEY_ADVANCE_HOLD_MS = 600;
constexpr uint32_t PROPS_REFRESH_INTERVAL_MS = 60000;

// Preview auto-rotate (IMU) tuning. The StickS3 accelerometer shares the
// internal I2C bus with the M5PM1 power chip (polled every POWER_BUTTON_POLL_MS
// via the raw Wire driver). Reading the accel through M5Unified's separate I2C
// path contends with those reads; a slow/contended read blocks the main loop
// and starves the LiveView stream. So: poll at a modest rate, time each read,
// and if reads are persistently slow or failing, give up on orientation
// tracking for the session so this best-effort convenience never degrades the
// preview.
constexpr uint32_t PREVIEW_ORIENTATION_POLL_MS = 500;
constexpr uint32_t IMU_SLOW_READ_US = 8000;    // a read over ~8ms means bus contention
constexpr uint8_t  IMU_MAX_READ_PROBLEMS = 6;  // disable polling after this many slow/failed reads

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
// BLE connection interval requested at connect, in 1.25ms units. GATT
// service/characteristic discovery -- the dominant cost of the first connect --
// is many sequential ATT round-trips, each taking one connection interval. The
// NimBLE default (30-50ms) made discovery/pre-warm take ~6s; a faster interval
// roughly halves that and also lowers shutter-write latency. 12 units = 15ms,
// 24 units = 30ms.
constexpr uint16_t BLE_CONN_ITVL_MIN_UNITS = 12;
constexpr uint16_t BLE_CONN_ITVL_MAX_UNITS = 24;
constexpr uint32_t RICOH_BLE_BONDED_SECURITY_WAIT_MS = 1500;
// Long enough for on-device (button) passkey entry. Note the GR IIIx itself
// times out its pairing prompt around 30s, so the effective ceiling is the
// camera's; the wait loop also exits early on disconnect. GR IV pairs via
// numeric-comparison auto-confirm and never touches this budget.
constexpr uint32_t RICOH_BLE_SECURITY_WAIT_MS = 45000;
constexpr uint8_t FIRST_BOOT_BLE_PAIRING_ATTEMPTS = 12;
constexpr uint32_t SERIAL_BOOT_WAIT_MS = 500;
constexpr uint32_t CAMERA_POWER_OFF_COOLDOWN_MS = 15000;
// Pause between fresh-reconnect re-samples while the camera is asleep. Each
// fresh connect re-reads operation mode (the only way to detect the user
// powering the camera on — a held link latches BLE_STARTUP; see
// RICOH_BLE_STARTUP_IS_STANDBY).
//
// DO NOT lower this aggressively. Verified on a real GR IIIx (2026-07-09): at a
// 2s cadence the camera never leaves BLE_STARTUP — every fresh connect reads
// 0x02 and the link drops with reason=534 (connection-establishment failure),
// so the stick loops forever and never reaches LiveView even after the user
// powers the camera on. The camera needs idle BLE time between attempts to
// settle out of BLE_STARTUP into CAPTURE; hammering it with reconnects keeps its
// BLE layer perpetually restarting. 8s is the known-good value (fresh connect
// reads CAPTURE ~9s after power-on).
constexpr uint32_t CAMERA_POWER_OFF_PROBE_BACKOFF_MS = 8000;
constexpr uint32_t BLE_MANUAL_WAKE_REINIT_SETTLE_MS = 3000;
// Fast auto-wake reconnect settle. The passive auto power-on path (P2 hybrid)
// only reaches the connect step AFTER a clean no-security probe has just
// connected, read operation-mode=CAPTURE, and disconnected -- proof the BLE
// stack and camera link are healthy. So it skips the heavy resetStack() +
// BLE_MANUAL_WAKE_REINIT_SETTLE_MS (~4.5s total) that the manual BtnA path needs
// to recover from a possibly-wedged stack, and waits only long enough for the
// camera to accept a fresh connection after the probe disconnect.
constexpr uint32_t BLE_AUTO_WAKE_RECONNECT_SETTLE_MS = 400;
constexpr int RICOH_BLE_DISCONNECT_REMOTE_USER = 0x213;
constexpr int RICOH_BLE_DISCONNECT_REMOTE_POWER_OFF = 0x215;
// Fallback camera-off detection for power-off events that DON'T report a clean
// remote disconnect reason. Verified on a real GR IIIx (2026-07): switching the
// camera off during a live session does not always surface reason 0x213/0x215 --
// the link can simply die (supervision timeout 0x208) and the Wi-Fi AP vanish.
// When that happens the reason-based power-off path never fires and recovery
// loops on "Connecting..." indefinitely. So if BLE recovery cannot re-establish
// the link across this many consecutive attempts OR within this window, assume
// the camera is off and enter the sleep guard. A camera that is merely glitching
// gets re-found by the scan and recovers before the threshold, so this will not
// misfire on transient link loss.
constexpr uint16_t CAMERA_OFF_RECOVERY_FALLBACK_ATTEMPTS = 3;
constexpr uint32_t CAMERA_OFF_RECOVERY_FALLBACK_MS = 6000;
// When reconnecting to a KNOWN (stored-identity) camera, a run of failed BLE
// connects means the camera is asleep/standby: RICOH keeps its BLE layer
// advertising when the camera is off, so the scan still finds it and tries to
// connect, but the connect fails to establish (or drops in security) and
// hammering it just keeps its BLE layer restarting. Rather than burn the full
// BLE_CONNECT_ATTEMPTS budget (~30s of "CONNECTING...") before giving up, drop
// into the sleep guard after this many consecutive connect failures. The guard
// shows "Camera off" and paces its own passive re-probe / auto-wake, so a camera
// that is actually being powered on is picked up there. First-boot pairing is
// exempt (there is no stored identity to fall back to).
constexpr uint8_t BLE_STANDBY_RECONNECT_MAX_CONNECT_FAILS = 3;
constexpr uint8_t RICOH_BLE_POWER_READ_RETRIES = 2;
constexpr uint8_t RICOH_BLE_OPERATION_MODE_READ_RETRIES = 2;
// BLE_STARTUP means the camera's main system is asleep while its BLE layer is up.
constexpr uint32_t RICOH_BLE_POWER_NOTIFY_SETTLE_MS = 30;

// Treat BLE_STARTUP as an asleep/standby state that must NOT be woken by writing
// WLAN-ON (which auto-wakes the camera and extends the lens — mechanically
// unsafe). Verified on a real GR IIIx (2026-07): a BLE link opened while the
// camera is asleep latches BLE_STARTUP for the link's entire lifetime — re-reads
// AND the operation-mode Notify both never update. The only way to observe the
// user powering the camera on is a FRESH reconnect: a new link established after
// power-on reads CAPTURE (0x00) and flows through to Wi-Fi/LiveView, whereas a
// new link while still asleep reads BLE_STARTUP again.
//
// With this true, a BLE_STARTUP read routes into the camera-off wait path
// (enterCameraSleepGuard): the stick sends NO WLAN-ON, disconnects, and the
// controller re-scans + reconnects on a cadence paced by
// CAMERA_POWER_OFF_PROBE_BACKOFF_MS, re-sampling the true mode on each fresh
// connect until the user powers the camera on. Set false to restore the legacy
// "drive WLAN-ON on the link to wake the camera" behavior.
constexpr bool RICOH_BLE_STARTUP_IS_STANDBY = true;

constexpr bool RICOH_BLE_REQUIRE_POWER_ON_BEFORE_WIFI = true;
constexpr bool RICOH_BLE_ALLOW_WIFI_WHEN_POWER_UNKNOWN = false;
constexpr bool RICOH_BLE_BLOCK_WIFI_IN_STANDBY_OPERATION_MODE = true;

// When the StickS3 is powered off (BtnPWR hold) while still connected to the
// camera over BLE, also command the camera to power off before the stick shuts
// down. This is done by writing 0x00 (Off) to the Camera Power characteristic
// (RICOH_BLE_POWER_STATE_HANDLE) — verified writable on the GR IIIx (GATT dump
// w=1) and documented Write/Mandatory (0=Off/1=On/2=Sleep) in the Ricoh GR BLE
// API. Set to false to leave the camera running when the remote turns off.
constexpr bool RICOH_BLE_POWER_OFF_CAMERA_ON_STICK_SHUTDOWN = true;

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

// P2 (Fable review) auto power-on detection HYBRID: when 1, while idling in the
// camera-off wait state the stick continuously LISTENS (passive/observe-only) for
// the camera's advertising power bit. Signal source proven by P0-A on a real GR
// IIIx (2026-07-09): the manufacturer-data last byte tracks power state --
// ...0300 while ASLEEP, ...0301 once the user powers the camera ON (independently
// corroborated by a ~10x advertising-interval change: ~320ms asleep vs ~33ms
// awake, and a ~5s gap-then-burst at the transition). On seeing the awake bit on
// RICOH_BLE_AWAKE_ADVERT_DEBOUNCE consecutive matched adverts, the stick fires a
// single READ-ONLY no-security operation-mode probe to confirm CAPTURE, then
// proceeds to the full connect -> Wi-Fi -> LiveView flow automatically.
// SAFETY: neither the passive scan nor the probe ever writes WLAN-ON, so this can
// never wake/extend the lens of a sleeping camera -- it only detects that the
// USER already powered it on. WLAN-ON stays gated behind the confirmed CAPTURE
// read. BtnA remains a manual override in all cases.
#ifndef RICOH_BLE_AUTO_PROBE
#define RICOH_BLE_AUTO_PROBE 1
#endif

// Auto-probe cadence: minimum spacing between passive awake-scan windows while
// idling. The passive scan itself is cheap (listen-only, never connects/writes),
// so keep this short to minimize the "dead gap" between windows -- a shorter gap
// means less worst-case latency between the user powering the camera on and us
// noticing the AWAKE advert. The scan window (RICOH_BLE_AWAKE_SCAN_WINDOW_MS)
// dominates the listen duty cycle regardless.
#ifndef RICOH_BLE_AUTO_PROBE_INTERVAL_MS
#define RICOH_BLE_AUTO_PROBE_INTERVAL_MS 300
#endif

// Manufacturer-data last-byte power bit (P0-A). Last byte == AWAKE => the user
// powered the camera on; == ASLEEP => still in standby.
constexpr uint8_t RICOH_BLE_ADV_POWER_BIT_AWAKE = 0x01;
constexpr uint8_t RICOH_BLE_ADV_POWER_BIT_ASLEEP = 0x00;
// Require the awake bit on this many back-to-back matched adverts before probing
// (debounce against a single stray/anomalous packet). P0-A showed the bit is
// rock-steady per state, so a small value is plenty.
constexpr uint8_t RICOH_BLE_AWAKE_ADVERT_DEBOUNCE = 2;
// Bounded duration of one passive awake-scan window. Awake adverts arrive
// ~33ms apart, so this easily captures the >=2 needed to confirm; asleep adverts
// arrive ~320ms apart, so a full window still yields several negative samples
// before we give up and idle until the next tick.
constexpr uint32_t RICOH_BLE_AWAKE_SCAN_WINDOW_MS = 2500;

// Require this many CONSECUTIVE no-security probes (each its own fresh connect,
// spaced RICOH_BLE_AUTO_PROBE_INTERVAL_MS/CAMERA_POWER_OFF_PROBE_BACKOFF_MS apart)
// to read operation-mode CAPTURE before trusting it and waking the connect flow.
// Verified on a real GR IIIx (2026-07-09): the advert power bit stays latched
// AWAKE for a while after the camera is switched off, so the passive gate above
// cannot filter it -- serviceAutoProbeIfDue() falls through to a real
// probeOperationModeNoSecurity() connect on every backoff tick while the guard
// is active. During the few seconds it takes the camera's BLE MCU to settle
// after power-off, a single such probe can misread CAPTURE (0x00) instead of
// the eventual steady Other/PowerOffTransfer, which used to trigger
// requestManualCameraWake() immediately -- a full, user-visible "Connecting..."
// flash that then fails and drops straight back to "Camera off" (seen as the
// screen cycling between the two). Requiring 2 in a row filters that transient
// noise: any correct Other/PowerOffTransfer read in between resets the streak.
// BtnA remains an instant manual override regardless of this debounce.
constexpr uint8_t RICOH_BLE_AUTO_PROBE_CAPTURE_CONFIRMATIONS = 2;

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
