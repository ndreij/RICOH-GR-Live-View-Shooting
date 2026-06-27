#pragma once

#include <Arduino.h>
#include <driver/gpio.h>

#ifndef GR_WIFI_SSID
#define GR_WIFI_SSID "GR_H264456"
#endif

#ifndef GR_WIFI_PASSWORD
#define GR_WIFI_PASSWORD ""
#endif

#ifndef GR_HOST
#define GR_HOST "192.168.0.1"
#endif

#ifndef GR_PORT
#define GR_PORT 80
#endif

constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t DISPLAY_HEIGHT = 135;

constexpr size_t FRAME_BUFFER_SIZE = 256 * 1024;
constexpr size_t STREAM_READ_BUFFER_SIZE = 2048;

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t PROPS_TIMEOUT_MS = 3500;
constexpr uint32_t LIVEVIEW_STALL_TIMEOUT_MS = 5000;
constexpr uint32_t UI_STATUS_INTERVAL_MS = 1000;
constexpr uint32_t PROPS_REFRESH_INTERVAL_MS = 60000;

#ifndef JPEG_SCALE_POLICY
#define JPEG_SCALE_POLICY JPEG_SCALE_HALF
#endif
constexpr uint32_t BLE_SCAN_SECONDS = 5;
constexpr uint32_t BLE_CONNECT_TIMEOUT_MS = 5000;
constexpr gpio_num_t G11_BUTTON_GPIO = GPIO_NUM_11;
constexpr uint32_t G11_DEBOUNCE_MS = 50;
constexpr uint32_t G11_LONG_PRESS_MS = 2000;
constexpr uint32_t SHUTTER_TIMEOUT_MS = 5000;

#ifndef RICOH_BLE_SERVICE_UUID_PRIMARY
#define RICOH_BLE_SERVICE_UUID_PRIMARY "009A8E70-B306-4451-B943-7F54392EB971"
#endif

#ifndef RICOH_BLE_SERVICE_UUID_ALT
#define RICOH_BLE_SERVICE_UUID_ALT "A0C10148-8865-4470-9631-8F36D79A41A5"
#endif
