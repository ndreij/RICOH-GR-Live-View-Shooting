# RICOH GR StickS3 Remote Viewfinder

M5Stack StickS3 firmware for a low-FPS live viewfinder for RICOH GR cameras.

The MVP implements only the verified live-preview path:

1. Connect StickS3 to the RICOH GR Wi-Fi AP.
2. Read camera status from `GET http://192.168.0.1/v1/props`.
3. Stream `GET http://192.168.0.1/v1/liveview`.
4. Extract JPEG frames from the multipart MJPEG stream by scanning `FF D8` → `FF D9`.
5. Decode and display frames on the StickS3 135×240 LCD in landscape mode.
6. Show Wi-Fi/liveview state and FPS.

## Hardware

- M5Stack StickS3
- RICOH GR III / GR IIIx or compatible GR model with Wi-Fi Remote Capture

The current configured camera Wi-Fi SSID is:

```text
GR_H264456
```

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

`platformio.ini` sets `GR_WIFI_SSID` to `GR_H264456`.

By default `GR_WIFI_PASSWORD` is empty because the provided documentation only
specified the SSID. If your camera requires a password, add a build flag:

```ini
build_flags =
    ...
    -DGR_WIFI_PASSWORD=\"your-camera-password\"
```

If no password is set, the firmware first tries an open AP connection, then
falls back to trying the SSID itself as the password.

Do not commit your camera Wi-Fi password. For local builds only, temporarily add
`-DGR_WIFI_PASSWORD="your-camera-password"` to `platformio.ini`, or pass an
equivalent private build flag in your local workflow.

## Display behavior

During active liveview the firmware uses pure viewfinder mode: no top/bottom overlay is drawn over the JPEG stream. FPS and decode timing are printed to serial logs instead. This avoids border flicker on the StickS3 LCD.

## Buttons

- **Button B**: toggle/reconnect liveview stream.
- **Button A**: reserved for future shutter control. It intentionally does
  nothing except print a serial message, because the real shutter endpoint is
  not verified yet.

## Expected serial logs

```text
RICOH GR StickS3 Remote Viewfinder
WiFi: connecting to GR_H264456
WiFi connected: 192.168.0.x RSSI=-45
Props: HTTP 200 ...
LiveView: connected
JPEG frame: 23841 bytes
Frame 1: 720x480 decode=58ms fps=3.2
```

## Troubleshooting

- If Wi-Fi never connects, verify the GR camera Wi-Fi AP is enabled and check
  whether the camera displays a password.
- If `/v1/props` succeeds but preview is black, verify Remote Capture/liveview
  is enabled on the camera.
- If frames stall for more than a few seconds, the firmware closes and
  reconnects the liveview TCP stream automatically.
- This is designed as a low-FPS composition aid, not a high-resolution monitor.

## Safety note

No shutter, AF, exposure, or camera-control POST endpoint is implemented in this
MVP. Those APIs should only be added after packet capture confirms the exact
RICOH Image Sync request.
