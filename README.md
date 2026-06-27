# RICOH GR StickS3 Remote Viewfinder

M5Stack StickS3 firmware for a RICOH GR remote viewfinder. V2 keeps the verified
low-FPS live preview and adds the second-version control skeleton from the design
documents: BLE camera discovery, persisted camera profile, Wi-Fi auto connect,
auto liveview start, and an external G11 shutter button framework.

## V2 feature set

- Boot-time BLE scan for RICOH GR advertisements.
- RICOH BLE service discovery for:
  - `009A8E70-B306-4451-B943-7F54392EB971`
  - `A0C10148-8865-4470-9631-8F36D79A41A5`
- Persisted camera profile in ESP32 NVS:
  - BLE name/address
  - Wi-Fi SSID/password/camera IP
  - optional shutter HTTP command configuration
- Wi-Fi auto connection to the stored/default camera profile.
- Auto start of `GET http://192.168.0.1/v1/liveview` after Wi-Fi connects.
- `GET /v1/status/device` and `GET /v1/props` status probes.
- GPIO11 external button support:
  - short press: trigger configured shutter command
  - long press: reconnect Wi-Fi/liveview
- Pure viewfinder drawing during liveview to avoid top/bottom LCD flicker.

## Current camera defaults

`platformio.ini` sets the default camera Wi-Fi SSID to:

```text
GR_H264456
```

The camera IP defaults to `192.168.0.1`.

## Important shutter note

The real RICOH GR shutter endpoint is still not verified by packet capture, so
this firmware does **not** hardcode a guessed capture API. G11 short press only
sends a request when a valid `ShutterCommandConfig` exists in NVS. Otherwise the
screen and serial log show:

```text
API NOT SET
```

This is intentional: it prevents accidental calls to an unconfirmed camera API.
Once the exact Image Sync request is captured, store that method/path/body in the
profile instead of changing random endpoints in code.

## Hardware

- M5Stack StickS3
- RICOH GR III / GR IIIx or compatible GR model with Wi-Fi Remote Capture
- Optional external momentary button wired between **GPIO11 (G11)** and **GND**

G11 is configured as `INPUT_PULLUP`, active-low, with a 50 ms debounce window.

## Build and flash

```bash
platformio run
platformio run -t upload
platformio device monitor
```

The firmware enables USB CDC on boot, so serial logs should appear at `115200`.

## PlatformIO board note

The project follows M5Stack's StickS3 PlatformIO example by using
`board = esp32-s3-devkitc-1` with `qio_opi` memory flags and
`BOARD_HAS_PSRAM`. The firmware checks `psramFound()` at boot and shows an
error if PSRAM is unavailable.

## Wi-Fi password

`platformio.ini` intentionally does not commit a camera Wi-Fi password. If your
camera requires a password, add a private local build flag:

```ini
build_flags =
    ...
    -DGR_WIFI_PASSWORD=\"your-camera-password\"
```

If no password is set, the firmware first tries an open AP connection, then
falls back to trying the SSID itself as the password. Do not commit your camera
Wi-Fi password.

## Runtime flow

1. Load camera profile from NVS, falling back to build defaults.
2. Scan BLE for a RICOH GR device and save the selected BLE identity.
3. Discover known RICOH BLE services, then disconnect BLE.
4. Connect to camera Wi-Fi.
5. Probe `/v1/status/device` and `/v1/props`.
6. Open `/v1/liveview`, parse MJPEG frames, decode JPEG, and draw to LCD.
7. Watchdog reconnects liveview if frames stall.

## Buttons

- **G11 short press**: send configured shutter HTTP command; if not configured,
  show `API NOT SET` and send nothing.
- **G11 long press**: reconnect Wi-Fi/liveview.
- **Button B**: toggle/reconnect liveview stream.
- **Button A**: reserved; it does not send a camera-control request.

## Expected serial logs

```text
RICOH GR StickS3 Remote Viewfinder V2
Profile: camera='' ble='' wifi='GR_H264456' ip='192.168.0.1' shutter=not-set
BLE: scanning 5 seconds, preferred=''
BLE: candidate addr=... rssi=-42 name='RICOH ...' primary=1 alt=0
BLE: services primary=1 alt=0
WiFi: connecting to 'GR_H264456' host=192.168.0.1
WiFi: connected ip=192.168.0.x rssi=-45
StatusDevice: ... bytes
Props: model='RICOH GR' battery='...'
LiveView: connected
JPEG frame: 32569 bytes, 720x480, decode=...
```

## Troubleshooting

- If BLE is not found, confirm the camera is in the pairing/remote mode expected
  by the design document. Wi-Fi/liveview can still continue from stored/default
  profile information.
- If Wi-Fi never connects, verify the GR camera Wi-Fi AP is enabled and check
  whether the camera displays a password.
- If `/v1/props` succeeds but preview is black, verify Remote Capture/liveview
  is enabled on the camera.
- If frames stall for more than a few seconds, the firmware closes and
  reconnects the liveview TCP stream automatically.
- If G11 shows `API NOT SET`, the shutter request is not configured yet; this is
  expected until the exact RICOH shutter API is verified.