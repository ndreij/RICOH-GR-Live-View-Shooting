# RICOH GR StickS3 Remote Viewfinder

[![Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/thinkerzhang)

运行在 M5Stack StickS3 上的 RICOH GR 无线实时取景和 BLE 遥控快门固件。

固件以 **BLE 作为相机发现、配对、唤醒和控制入口**：先通过 BLE 找到并安全连接相机，再按相机当前状态决定是否发送 Wi-Fi ON 指令；Wi-Fi 启动后读取相机动态返回的 WLAN 参数，最后通过 RICOH HTTP API 打开 LiveView 并在 StickS3 屏幕显示 MJPEG 预览。

[English README](README_EN.md)

---

## 当前能力

- **BLE 优先连接**：首次使用时扫描 `GR_` / RICOH 设备并完成安全配对；后续启动优先使用 NVS 中保存的 BLE 地址、地址类型和 bonded 状态进行快速直连，失败后回退扫描。
- **关机/待机保护**：StickS3 重启后即使能连上相机 BLE，也会先读取 RICOH Power State 和 Operation Mode；当相机处于 `BLE_STARTUP` / `POWER_OFF_TRANSFER` 等待机广播模式时，不会自动发送 Wi-Fi ON，避免把关机状态下的相机唤醒。
- **动态 Wi-Fi 参数**：不依赖 `platformio.ini` 中的固定 SSID / 密码。相机 Wi-Fi 打开后，通过 BLE 读取 SSID、密码、频率/信道和 BSSID。
- **Wi-Fi 参数缓存**：成功连接后将 Wi-Fi 参数绑定到 BLE 地址缓存；下次启动会先用缓存信道/BSSID 快速尝试，失败后自动回读最新 BLE 参数，避免相机 WLAN 参数变化导致卡死。
- **LiveView 实时预览**：连接相机 Wi-Fi AP 后打开 `/v1/liveview`，解析 MJPEG 并显示到 StickS3 屏幕；相机属性通过 HTTP 周期刷新。
- **正式 RICOH 快门协议**：Button A 通过 RICOH Shooting Service 写入 `ShootingFlavor=IMMEDIATE` 和 `OperationRequest={START, AF}`，不再使用旧的 1-byte focus/shoot/release 写法。
- **手动唤醒恢复**：进入 `CAMERA_SLEEP_GUARD` 后不会自动唤醒相机；冷却结束后按 Button A 才会重建 NimBLE 栈并重新连接。
- **掉线恢复**：LiveView、Wi-Fi 或 BLE 掉线时会按当前状态选择 BLE_READY 重试或重新扫描，避免无限扩大恢复范围。

---

## 相机兼容性状态

当前代码和协议参数只在 **RICOH GR IV HDF** 上完成实机验证。

| 相机 | 状态 | 说明 |
| --- | --- | --- |
| RICOH GR IV HDF | 已验证可用 | 当前主要开发和测试机型 |
| RICOH GR IV 系列 | 理论可用 | 同代 BLE / Wi-Fi / HTTP LiveView 协议预计兼容，但仍建议实机确认 |
| RICOH GR III / GR IIIx | 当前不可用 | 与当前 GR IV 实现存在协议/行为差异，不作为本固件支持目标 |
| RICOH GR II | 当前不可用 | 不支持当前固件使用的 GR IV BLE-first 流程 |

README 中列出的 BLE UUID、handle、Operation Mode 和 Shooting Service 流程均以 GR IV HDF 实测为准。

---

## 实际启动和连接流程

```text
StickS3 上电 / 重启
  -> 初始化屏幕、M5PM1 电源、按键、JPEG 解码、Wi-Fi STA、NVS profile
  -> runCameraFlowOnce()
  -> BLE_SCAN
     -> 有已保存 BLE 身份：快速直连保存的 BLE 地址
     -> 无保存身份或直连失败：扫描 GR / RICOH 广播设备
  -> BLE 安全连接 / 加密 / 保存相机身份
  -> BLE_READY
  -> 读取 Power State (0x00EB)
  -> 读取 Operation Mode (1452335A-EC7F-4877-B8AB-0F72E18BB295)
     -> CAPTURE / PLAYBACK / OTHER：允许继续
     -> BLE_STARTUP / POWER_OFF_TRANSFER：进入 CAMERA_SLEEP_GUARD，不发送 Wi-Fi ON
  -> 写入 WLAN ON (0x0135 = 0x01)
  -> 优先尝试缓存 Wi-Fi 参数（短超时）
     -> 成功：直接进入 LiveView，随后延迟刷新 BLE Wi-Fi 缓存
     -> 失败：读取最新 BLE Wi-Fi 参数并重新连接
  -> 连接相机 Wi-Fi AP
  -> 打开 /v1/liveview
  -> LIVEVIEW_RUNNING
```

### 为什么要读取 Operation Mode

RICOH GR 在“看起来关机/待机”的状态下仍可能允许 BLE 连接，并且 Power State 读到 `0x01`。如果只看 Power State，StickS3 会误认为相机已开机，然后发送 Wi-Fi ON，从而把相机唤醒。

现在固件会在发送 Wi-Fi ON 前额外读取 Operation Mode：

- `CAPTURE` / `PLAYBACK`：相机处于可用工作状态，允许打开 Wi-Fi。
- `BLE_STARTUP` / `POWER_OFF_TRANSFER`：相机处于待机/关机相关 BLE 状态，自动流程进入 `CAMERA_SLEEP_GUARD`。
- 手动按 Button A 时会设置 manual wake override，允许用户主动唤醒。

---

## 相机关机保护流程

```text
LiveView 运行中 / StickS3 重启后连接到待机相机
  -> 检测到 BLE 断连原因 0x213 / 0x215，或 Operation Mode 为 BLE_STARTUP / POWER_OFF_TRANSFER
  -> 关闭 LiveView，断开 Wi-Fi 和 BLE
  -> 进入 CAMERA_SLEEP_GUARD
  -> 15 秒冷却期内禁止自动扫描、自动重连和 Wi-Fi ON
  -> 冷却结束后仍不自动唤醒相机
  -> 用户按 Button A
  -> 清除 guard，重建 NimBLE 栈，重新扫描/连接/打开 Wi-Fi
```

典型日志：

```text
BLE: operation mode read value=0x02 state=BLE_STARTUP
WiFi blocked: camera operation mode=BLE_STARTUP while power=ON source=WiFi open
Flow: BLE_READY -> CAMERA_SLEEP_GUARD (BLE operation mode standby)
BLE guard: remote disconnect reason=533; auto wake paused for 15s, then manual wake required
```

此时不应再出现：

```text
BLE: Wi-Fi open requested
```

除非用户按 Button A 手动唤醒。

---

## 按键操作

| 按键 | 行为 |
| --- | --- |
| Button A | 正常 LiveView 中触发 BLE AF 快门；在 `CAMERA_SLEEP_GUARD` 中作为手动唤醒 / 重连键 |
| 电源键长按 | 关闭 StickS3：关闭 LiveView、断开 Wi-Fi/BLE，然后执行 M5PM1 / M5Unified 关机 |

Button A 使用 `M5.BtnA.wasPressed()` 轮询；电源键同时使用 M5Unified hold 事件和 M5PM1 状态轮询增强稳定性。

---

## RICOH BLE / HTTP 协议点

### BLE 服务与特征

以下协议点基于 GR IV HDF 实测整理；GR III / GR II 不适用。

| 功能 | UUID / Handle | 操作 | 说明 |
| --- | --- | --- | --- |
| Camera Service | `4B445988-CAA0-4DD3-941D-37B4F52ACA86` | Service | Power / Operation Mode 所在服务 |
| Power State | handle `0x00EB` | Read / Notify | `0x01` 表示 BLE 可控制；`0x00` 表示关机或关机中 |
| Power State CCCD | handle `0x00EC` | Write | 写 `0x01 0x00` 订阅电源通知 |
| Operation Mode | `1452335A-EC7F-4877-B8AB-0F72E18BB295` | Read | 用于区分 `CAPTURE`、`PLAYBACK`、`BLE_STARTUP`、`POWER_OFF_TRANSFER` |
| WLAN Power | handle `0x0135` | Write | 写 `0x01` 请求相机打开 Wi-Fi |
| WLAN SSID | handle `0x0138` | Read | 读取相机 AP SSID |
| WLAN Passphrase | handle `0x013A` | Read | 读取相机 AP 密码 |
| WLAN Security | handle `0x013C` | Read | 读取加密类型 |
| WLAN Frequency | handle `0x013E` | Read | 读取频率/信道提示 |
| WLAN BSSID | handle `0x0140` | Read | 读取或辅助解析 AP BSSID；实际连接成功后也会学习 `WiFi.BSSIDstr()` |
| Shooting Service | `9F00F387-8345-4BBC-8B92-B87B52E3091A` | Service | RICOH 拍摄控制服务 |
| Shooting Flavor | `B29E6DE3-1AEC-48C1-9D05-02CEA57CE664` | Write | Button A 拍摄前写 `0x00`（Immediate） |
| Operation Request | `559644B8-E0BC-4011-929B-5CF9199851E7` | Write | AF 拍摄写 `{0x01, 0x01}`；无 AF 写 `{0x01, 0x00}` |

### HTTP API

- 相机默认地址：`192.168.0.1`
- LiveView：`/v1/liveview`
- 相机属性：`/v1/props`

---

## Wi-Fi 缓存策略

缓存存储在 NVS，绑定到当前 BLE 地址：

- SSID / passphrase
- BSSID
- frequency / channel
- camera IP

连接策略：

1. BLE 已连接且 Wi-Fi ON 已请求后，才允许使用缓存。
2. 有缓存时先等待 `WIFI_CACHED_CONNECT_GRACE_MS`，再用 `WIFI_CACHED_CONNECT_TIMEOUT_MS` 短超时尝试。
3. 缓存失败不会卡住，会回读最新 BLE Wi-Fi 参数。
4. 成功连接后会学习实际 `WiFi.BSSIDstr()` 并更新缓存。
5. 缓存路径成功后会延迟刷新 BLE 参数，避免相机 WLAN 参数变化时下次启动仍使用旧缓存。

---

## 构建、烧录和串口监视

```bash
# 编译默认 m5stack-sticks3 环境
platformio run

# 烧录
platformio run --target upload

# 指定串口烧录示例
platformio run --target upload --upload-port COM6

# 串口监视
platformio device monitor --port COM6 --baud 115200 --filter time

# host-side native 测试（不需要相机或 StickS3）
platformio test -e native
```

串口波特率：`115200`

默认 PlatformIO 环境：`m5stack-sticks3`。目标硬件为 ESP32-S3 DevKitC-1 N8 / M5Stack StickS3，开启 PSRAM 相关编译选项。

---

## 关键配置

主要配置文件：

- `src/config.h`
- `platformio.ini`

| 参数 | 默认值 | 说明 |
| --- | ---: | --- |
| `BLE_SCAN_SECONDS` | `2` | 单轮 BLE 扫描时间 |
| `BLE_FAST_CONNECT_TIMEOUT_MS` | `3000` | 使用已保存 BLE 地址直连的超时 |
| `BLE_CONNECT_TIMEOUT_MS` | `8000` | 扫描后 BLE 连接超时 |
| `BLE_CONNECT_ATTEMPTS` | `12` | 已有身份时的扫描/连接尝试次数 |
| `FIRST_BOOT_BLE_PAIRING_ATTEMPTS` | `12` | 首次配对或 NVS 无身份时的扫描轮数 |
| `RICOH_BLE_BONDED_SECURITY_WAIT_MS` | `1500` | bonded 直连时等待加密完成的时间 |
| `RICOH_BLE_SECURITY_WAIT_MS` | `7000` | 首次配对/非 bonded 时等待加密完成的时间 |
| `RICOH_BLE_POWER_READ_RETRIES` | `2` | Wi-Fi ON 前 Power State 读取重试次数 |
| `RICOH_BLE_OPERATION_MODE_READ_RETRIES` | `2` | Wi-Fi ON 前 Operation Mode 读取重试次数 |
| `RICOH_BLE_BLOCK_WIFI_IN_STANDBY_OPERATION_MODE` | `true` | 在 `BLE_STARTUP` / `POWER_OFF_TRANSFER` 下阻止自动 Wi-Fi ON |
| `WIFI_CACHED_CONNECT_GRACE_MS` | `700` | Wi-Fi ON 后缓存连接前的短等待 |
| `WIFI_CACHED_CONNECT_TIMEOUT_MS` | `1200` | 缓存 Wi-Fi 参数连接短超时 |
| `WIFI_CHANNEL_HINT_CONNECT_TIMEOUT_MS` | `6000` | 使用信道提示连接 Wi-Fi 的超时 |
| `WIFI_CONNECT_TIMEOUT_MS` | `15000` | Wi-Fi 总连接超时 |
| `WIFI_CACHE_REFRESH_DELAY_MS` | `5000` | 缓存连接成功后延迟刷新 BLE Wi-Fi 参数 |
| `CAMERA_POWER_OFF_COOLDOWN_MS` | `15000` | 进入相机 guard 后的冷却时间 |
| `BLE_MANUAL_WAKE_REINIT_SETTLE_MS` | `3000` | 手动唤醒时 NimBLE 栈重建后的稳定等待 |
| `LIVEVIEW_STALL_TIMEOUT_MS` | `5000` | LiveView 无有效帧时的恢复阈值 |

---

## 典型日志

### 正常连接

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

### 缓存 Wi-Fi 连接

```text
WiFi cache: waiting 700ms for camera AP before cached connect
WiFi cache: trying cached params ssid='GR_H264456' bssid='F2:3E:05:26:45:56' channel=1 short_timeout=1200ms
WiFi: begin channel=1 has_bssid=1 timeout=1200ms
WiFi: connect completed in ...ms channel=1 status=CONNECTED
WiFi cache: saved (cached connect) ssid='GR_H264456' bssid='F2:3E:05:26:45:56' channel=1 freq=2412
```

### 待机相机不会被自动唤醒

```text
BLE: power handle=0x00EB read value=0x01
BLE: operation mode read value=0x02 state=BLE_STARTUP
WiFi blocked: camera operation mode=BLE_STARTUP while power=ON source=WiFi open
Flow: BLE_READY -> CAMERA_SLEEP_GUARD (BLE operation mode standby)
```

### Button A 快门

```text
BLE: Ricoh shutter OperationRequest START param=1 autofocus=1
```

---

## 故障排查

### StickS3 重启后相机不应被自动唤醒

这是当前正式行为。只要 Operation Mode 是 `BLE_STARTUP` 或 `POWER_OFF_TRANSFER`，固件会进入 `CAMERA_SLEEP_GUARD`，不会发送 `BLE: Wi-Fi open requested`。需要按 Button A 才会手动唤醒。

### 缓存 Wi-Fi 第一次尝试失败是否正常

正常。相机 AP 刚打开时可能还没准备好。固件会用短超时尝试缓存，失败后立刻通过 BLE 读取最新 WLAN 参数并重连。

### Button A 没有触发 AF

先确认串口是否出现：

```text
BLE: Ricoh shutter OperationRequest START param=1 autofocus=1
```

如果日志存在但相机没有 AF，请检查相机当前对焦模式、快门/AF 设置以及相机是否允许 BLE 远程 AF。固件侧已经走 RICOH Shooting Service 的 AF 参数。

### 串口监视出现 `ClearCommError failed`

这是 Windows / PlatformIO 串口在设备重启时的重连提示，一般不代表固件异常；等待 monitor 自动重新连接即可。

---

## 项目结构

```text
src/
  main.cpp                 主状态机、连接流程、待机保护、按键逻辑
  ricoh_ble_client.*       RICOH BLE 扫描、连接、安全配对、Wi-Fi 参数读取、OperationMode、AF 快门
  gr_wifi.*                ESP32 Wi-Fi STA 连接、信道/BSSID 优化
  gr_api.*                 RICOH HTTP API 与 LiveView
  camera_profile_store.*   NVS 相机身份与 Wi-Fi 缓存
  camera_identity.*        Wi-Fi SSID 到 BLE 名称的推导逻辑
  ble_reconnect_policy.*   BLE 地址类型/直连策略辅助
  mjpeg_stream.*           MJPEG 流解析
  jpeg_decoder.*           JPEG 解码与显示输出
  display.*                StickS3 屏幕 UI
  buttons.*                StickS3 按键轮询

test/
  test_native/             host-side 逻辑测试
```

---

## 许可证

本项目基于 GNU General Public License v3.0 开源。

你可以在 GPL-3.0 条款下使用、修改和分发本项目。如果分发修改后的版本或基于本项目的衍生作品，也需要按照 GPL-3.0 协议开放对应源代码。

详情请查看 [LICENSE](LICENSE)。
