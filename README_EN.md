# RICOH GR StickS3 Remote Viewfinder

[![Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/thinkerzhang)

Firmware for running RICOH GR wireless live view and BLE remote shutter on the M5Stack StickS3.

The firmware uses **BLE as the entry point for discovery, pairing, wake control, and shutter control**. It connects securely to the camera over BLE, checks whether the camera is really in an active mode before sending Wi-Fi ON, reads the dynamic WLAN parameters returned by the camera, then opens the RICOH HTTP LiveView stream and displays MJPEG frames on the StickS3 screen.

[中文 README](README.md)

---

## Current Capabilities

- **BLE-first connection**: On first use, scans for `GR_` / RICOH devices and performs secure pairing. On later boots, fast direct reconnect uses the saved BLE address, address type, and bonded state from NVS before falling back to scanning.
- **Power-off / standby guard**: After a StickS3 reboot, the firmware may still connect to a camera that is in BLE standby. It reads both RICOH Power State and Operation Mode before Wi-Fi ON; `BLE_STARTUP` and `POWER_OFF_TRANSFER` block automatic Wi-Fi wake.
- **Dynamic Wi-Fi credentials**: No fixed SSID/passphrase is required in `platformio.ini`. After Wi-Fi is enabled, SSID, passphrase, frequency/channel, and BSSID are read over BLE.
- **Wi-Fi credential cache**: Successful Wi-Fi parameters are cached and bound to the BLE address. The next boot tries cached channel/BSSID with a short timeout and falls back to fresh BLE parameters if the camera WLAN settings changed.
- **LiveView preview**: Connects to the camera Wi-Fi AP, opens `/v1/liveview`, parses MJPEG, and renders frames on the StickS3 display. Camera properties are refreshed periodically over HTTP.
- **RICOH shutter protocol**: Button A writes `ShootingFlavor=IMMEDIATE` and `OperationRequest={START, AF}` through the RICOH Shooting Service. The old 1-byte focus/shoot/release handle sequence is no longer used.
- **Manual wake and reconnect**: Once `CAMERA_SLEEP_GUARD` is active, the firmware will not wake the camera automatically. After cooldown, Button A manually rebuilds the NimBLE stack and reconnects.
- **Drop recovery**: LiveView, Wi-Fi, and BLE failures are recovered according to the current flow state, preferring BLE_READY retries before a full scan when possible.

---

## Camera Compatibility Status

The current code and protocol parameters have been verified on **RICOH GR IV HDF** only.

| Camera | Status | Notes |
| --- | --- | --- |
| RICOH GR IV HDF | Verified working | Primary development and test camera |
| RICOH GR IV series | Expected to work | Same-generation BLE / Wi-Fi / HTTP LiveView protocol is expected to be compatible, but real-device confirmation is still recommended |
| RICOH GR III / GR IIIx | Not currently supported | Protocol and behavior differ from the current GR IV implementation |
| RICOH GR II | Not currently supported | Does not support the GR IV BLE-first flow used by this firmware |

All BLE UUIDs, handles, Operation Mode behavior, and Shooting Service flow documented below are based on GR IV HDF testing.

---

## Actual Boot and Connection Flow

```text
StickS3 power on / reboot
  -> Initialize display, M5PM1 power, buttons, JPEG decoder, Wi-Fi STA, NVS profile
  -> runCameraFlowOnce()
  -> BLE_SCAN
     -> Saved BLE identity: fast direct reconnect to saved address
     -> No saved identity or direct reconnect failed: scan GR / RICOH advertisements
  -> Secure BLE connection / encryption / save camera identity
  -> BLE_READY
  -> Read Power State (0x00EB)
  -> Read Operation Mode (1452335A-EC7F-4877-B8AB-0F72E18BB295)
     -> CAPTURE / PLAYBACK / OTHER: continue
     -> BLE_STARTUP / POWER_OFF_TRANSFER: enter CAMERA_SLEEP_GUARD, do not send Wi-Fi ON
  -> Write WLAN ON (0x0135 = 0x01)
  -> Try cached Wi-Fi parameters first with a short timeout
     -> Success: open LiveView immediately, refresh BLE Wi-Fi cache later
     -> Failure: read fresh BLE Wi-Fi parameters and reconnect
  -> Connect to camera Wi-Fi AP
  -> Open /v1/liveview
  -> LIVEVIEW_RUNNING
```

### Why Operation Mode Is Checked

A RICOH GR can still accept BLE connections while it appears powered off or in standby, and Power State can read back as `0x01`. If the firmware only trusts Power State, it may send Wi-Fi ON and wake the camera unintentionally.

The current firmware reads Operation Mode before sending Wi-Fi ON:

- `CAPTURE` / `PLAYBACK`: camera is in an active usable state, Wi-Fi ON is allowed.
- `BLE_STARTUP` / `POWER_OFF_TRANSFER`: camera is in a standby or power-off transfer related BLE mode, automatic flow enters `CAMERA_SLEEP_GUARD`.
- Pressing Button A from the guard state sets a manual wake override and allows an intentional wake.

---

## Camera Power-Off Guard Flow

```text
LiveView running / StickS3 reboot connects to a standby camera
  -> BLE disconnect reason 0x213 / 0x215, or Operation Mode is BLE_STARTUP / POWER_OFF_TRANSFER
  -> Close LiveView, disconnect Wi-Fi and BLE
  -> Enter CAMERA_SLEEP_GUARD
  -> During 15-second cooldown, automatic scan/reconnect/Wi-Fi ON is blocked
  -> After cooldown, still do not wake the camera automatically
  -> User presses Button A
  -> Clear guard, rebuild NimBLE stack, scan/connect/open Wi-Fi again
```

Typical log:

```text
BLE: operation mode read value=0x02 state=BLE_STARTUP
WiFi blocked: camera operation mode=BLE_STARTUP while power=ON source=WiFi open
Flow: BLE_READY -> CAMERA_SLEEP_GUARD (BLE operation mode standby)
BLE guard: remote disconnect reason=533; auto wake paused for 15s, then manual wake required
```

In this case the firmware should not print:

```text
BLE: Wi-Fi open requested
```

unless the user presses Button A for manual wake.

---

## Controls

| Control | Behavior |
| --- | --- |
| Button A | Triggers BLE AF shutter during normal LiveView; acts as manual wake / reconnect while in `CAMERA_SLEEP_GUARD` |
| Long power key press | Shuts down the StickS3: closes LiveView, disconnects Wi-Fi/BLE, then powers off through M5PM1 / M5Unified |

Button A is polled with `M5.BtnA.wasPressed()`. The power key uses both M5Unified hold events and M5PM1 state polling for better reliability.

---

## RICOH BLE / HTTP Protocol Points

### BLE Services and Characteristics

The protocol points below are based on GR IV HDF testing; they do not apply to GR III / GR II.

| Feature | UUID / Handle | Operation | Description |
| --- | --- | --- | --- |
| Camera Service | `4B445988-CAA0-4DD3-941D-37B4F52ACA86` | Service | Service containing Power / Operation Mode |
| Power State | handle `0x00EB` | Read / Notify | `0x01` means BLE-controllable; `0x00` means powered off or shutting down |
| Power State CCCD | handle `0x00EC` | Write | Write `0x01 0x00` to subscribe to power notifications |
| Operation Mode | `1452335A-EC7F-4877-B8AB-0F72E18BB295` | Read | Distinguishes `CAPTURE`, `PLAYBACK`, `BLE_STARTUP`, and `POWER_OFF_TRANSFER` |
| WLAN Power | handle `0x0135` | Write | Write `0x01` to request camera Wi-Fi ON |
| WLAN SSID | handle `0x0138` | Read | Camera AP SSID |
| WLAN Passphrase | handle `0x013A` | Read | Camera AP password |
| WLAN Security | handle `0x013C` | Read | Security type |
| WLAN Frequency | handle `0x013E` | Read | Frequency/channel hint |
| WLAN BSSID | handle `0x0140` | Read | AP BSSID when provided; actual connected BSSID is also learned via `WiFi.BSSIDstr()` |
| Shooting Service | `9F00F387-8345-4BBC-8B92-B87B52E3091A` | Service | RICOH capture control service |
| Shooting Flavor | `B29E6DE3-1AEC-48C1-9D05-02CEA57CE664` | Write | Button A writes `0x00` (Immediate) before capture |
| Operation Request | `559644B8-E0BC-4011-929B-5CF9199851E7` | Write | AF capture writes `{0x01, 0x01}`; no-AF capture writes `{0x01, 0x00}` |

### HTTP API

- Default camera address: `192.168.0.1`
- LiveView: `/v1/liveview`
- Camera properties: `/v1/props`

---

## Wi-Fi Cache Strategy

The cache is stored in NVS and bound to the current BLE address:

- SSID / passphrase
- BSSID
- frequency / channel
- camera IP

Connection strategy:

1. Cached parameters are used only after BLE is connected and Wi-Fi ON has been requested.
2. With a valid cache, the firmware waits `WIFI_CACHED_CONNECT_GRACE_MS`, then tries a short `WIFI_CACHED_CONNECT_TIMEOUT_MS` connection using channel/BSSID hints.
3. A cache failure does not block startup; the firmware immediately reads fresh Wi-Fi parameters over BLE.
4. After a successful connection, the actual `WiFi.BSSIDstr()` is learned and saved.
5. When the cached path succeeds, BLE Wi-Fi parameters are refreshed later so the next boot will not keep stale WLAN settings.

---

## Build, Flash, and Monitor

```bash
# Build the default m5stack-sticks3 environment
platformio run

# Upload
platformio run --target upload

# Upload to a specific serial port example
platformio run --target upload --upload-port COM6

# Monitor serial logs
platformio device monitor --port COM6 --baud 115200 --filter time

# Run host-side native tests (no camera or StickS3 required)
platformio test -e native
```

Serial baud rate: `115200`

The default PlatformIO environment is `m5stack-sticks3`. The target is ESP32-S3 DevKitC-1 N8 / M5Stack StickS3 with PSRAM-related build flags enabled.

---

## Key Configuration

Main configuration files:

- `src/config.h`
- `platformio.ini`

| Parameter | Default | Description |
| --- | ---: | --- |
| `BLE_SCAN_SECONDS` | `2` | Duration of one BLE scan round |
| `BLE_FAST_CONNECT_TIMEOUT_MS` | `3000` | Timeout for fast direct reconnect to the saved BLE address |
| `BLE_CONNECT_TIMEOUT_MS` | `8000` | Timeout for BLE connect after scanning |
| `BLE_CONNECT_ATTEMPTS` | `12` | Scan/connect attempts when a camera identity is stored |
| `FIRST_BOOT_BLE_PAIRING_ATTEMPTS` | `12` | Scan rounds on first pairing or when NVS has no identity |
| `RICOH_BLE_BONDED_SECURITY_WAIT_MS` | `1500` | Security wait for bonded direct reconnect |
| `RICOH_BLE_SECURITY_WAIT_MS` | `7000` | Security wait for first pairing / non-bonded connect |
| `RICOH_BLE_POWER_READ_RETRIES` | `2` | Power State read retries before Wi-Fi ON |
| `RICOH_BLE_OPERATION_MODE_READ_RETRIES` | `2` | Operation Mode read retries before Wi-Fi ON |
| `RICOH_BLE_BLOCK_WIFI_IN_STANDBY_OPERATION_MODE` | `true` | Block automatic Wi-Fi ON in `BLE_STARTUP` / `POWER_OFF_TRANSFER` |
| `WIFI_CACHED_CONNECT_GRACE_MS` | `700` | Short delay after Wi-Fi ON before cached connect |
| `WIFI_CACHED_CONNECT_TIMEOUT_MS` | `1200` | Short timeout for cached Wi-Fi connect |
| `WIFI_CHANNEL_HINT_CONNECT_TIMEOUT_MS` | `6000` | Wi-Fi connection timeout when channel hint is available |
| `WIFI_CONNECT_TIMEOUT_MS` | `15000` | Total Wi-Fi connection timeout |
| `WIFI_CACHE_REFRESH_DELAY_MS` | `5000` | Delayed BLE Wi-Fi refresh after cached success |
| `CAMERA_POWER_OFF_COOLDOWN_MS` | `15000` | Cooldown after entering camera guard |
| `BLE_MANUAL_WAKE_REINIT_SETTLE_MS` | `3000` | Settling delay after NimBLE stack rebuild during manual wake |
| `LIVEVIEW_STALL_TIMEOUT_MS` | `5000` | LiveView stall threshold before recovery |

---

## Typical Logs

### Normal connection

```text
BLE: connected secure connect_ms=...
Flow: BLE_SCAN -> BLE_READY (BLE connected)
BLE: power handle=0x00EB read value=0x01
BLE: operation mode read value=0x00 state=CAPTURE
BLE: power notify enabled cccd=0x00EC
BLE: Wi-Fi open requested
BLE: Wi-Fi parameters received ssid='GR_H264456' bssid='' freq=2412 channel=1
WiFi: connected ip=192.168.0.4 rssi=-40
Flow: WIFI_CONNECTING -> LIVEVIEW_RUNNING (LiveView opened)
LiveView: connected
```

### Cached Wi-Fi connection

```text
WiFi cache: waiting 700ms for camera AP before cached connect
WiFi cache: trying cached params ssid='GR_H264456' bssid='F2:3E:05:26:45:56' channel=1 short_timeout=1200ms
WiFi: begin channel=1 has_bssid=1 timeout=1200ms
WiFi: connect completed in ...ms channel=1 status=CONNECTED
WiFi cache: saved (cached connect) ssid='GR_H264456' bssid='F2:3E:05:26:45:56' channel=1 freq=2412
```

### Standby camera is not auto-woken

```text
BLE: power handle=0x00EB read value=0x01
BLE: operation mode read value=0x02 state=BLE_STARTUP
WiFi blocked: camera operation mode=BLE_STARTUP while power=ON source=WiFi open
Flow: BLE_READY -> CAMERA_SLEEP_GUARD (BLE operation mode standby)
```

### Button A shutter

```text
BLE: Ricoh shutter OperationRequest START param=1 autofocus=1
```

---

## Troubleshooting

### The camera should not wake automatically after StickS3 reboots

This is the current intended behavior. If Operation Mode is `BLE_STARTUP` or `POWER_OFF_TRANSFER`, the firmware enters `CAMERA_SLEEP_GUARD` and does not print `BLE: Wi-Fi open requested`. Press Button A to wake the camera manually.

### The first cached Wi-Fi attempt fails

This is normal when the camera AP is still starting. The firmware uses a short cache timeout and falls back to fresh BLE WLAN parameters automatically.

### Button A does not trigger AF

First confirm the serial log contains:

```text
BLE: Ricoh shutter OperationRequest START param=1 autofocus=1
```

If the log is present but the camera does not autofocus, check the camera focus mode, shutter/AF settings, and whether BLE remote AF is allowed by the camera state. The firmware side is already sending the RICOH Shooting Service AF parameter.

### Serial monitor prints `ClearCommError failed`

This is usually a Windows / PlatformIO serial reconnect message when the device resets. It does not necessarily indicate a firmware fault; wait for the monitor to reconnect.

---

## Project Structure

```text
src/
  main.cpp                 Main state machine, connection flow, standby guard, button logic
  ricoh_ble_client.*       RICOH BLE scan/connect/security, Wi-Fi parameter reads, OperationMode, AF shutter
  gr_wifi.*                ESP32 Wi-Fi STA connection, channel/BSSID optimization
  gr_api.*                 RICOH HTTP API and LiveView
  camera_profile_store.*   NVS camera identity and Wi-Fi cache
  camera_identity.*        Wi-Fi SSID to BLE name derivation
  ble_reconnect_policy.*   BLE address type / direct reconnect helpers
  mjpeg_stream.*           MJPEG stream parser
  jpeg_decoder.*           JPEG decoding and display output
  display.*                StickS3 screen UI
  buttons.*                StickS3 button polling

test/
  test_native/             Host-side logic tests
```

---

## License

This project is licensed under the GNU General Public License v3.0.

You may use, modify, and distribute this project under the terms of the GPL-3.0 license. If you distribute modified versions or derivative works, you must also release the corresponding source code under the same license.

See [LICENSE](LICENSE) for details.
