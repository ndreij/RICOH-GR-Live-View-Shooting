# Project Overview

> 最近一次核对：2026-07-09（对照当前 `main` 分支源码逐项复查）。

## 目标

本项目是 RICOH GR Live View Shooting 固件。目标设备为 ESP32-S3 / M5Stack StickS3，使用 PlatformIO Arduino framework。固件通过 BLE 发现和控制 RICOH GR 相机，通过相机 Wi-Fi AP 使用 HTTP API 打开 LiveView，将 MJPEG 解码显示到 StickS3 屏幕，并提供 Button A BLE AF 快门。

## 已从代码确认的事实

- `platformio.ini` 默认环境：`m5stack-sticks3`。另有 `m5stack-sticks3-gr3x`（GR IIIx，完整 LiveView）、`m5stack-sticks3-gr3x-shutter`（GR IIIx，仅 BLE 快门不开 Wi-Fi）、`m5stack-sticks3-fastpreview`（GR IV，quarter-scale 调速用）、`native`（host 单元测试）。
- Platform：`espressif32@6.12.0`。
- Board：`esp32-s3-devkitc-1`。
- Framework：Arduino。
- 依赖：M5Unified、M5PM1、JPEGDEC、ArduinoJson、NimBLE-Arduino 2.5.0、WiFi、Preferences、Wire。
- 显示默认尺寸：`DISPLAY_WIDTH=240`、`DISPLAY_HEIGHT=135`。
- LiveView frame buffer：`FRAME_BUFFER_SIZE=256 * 1024`。
- Stream read buffer：`STREAM_READ_BUFFER_SIZE=2048`。
- Camera HTTP 默认地址：`GR_HOST=192.168.0.1`，`GR_PORT=80`。
- **主状态机已重构**：`CameraFlowState`（`src/main.cpp:43`）现在只是 `rvf::AppState`（定义于 `src/app/AppState.h`）的类型别名，不再是独立的 6 成员枚举。当前成员共 21 个：`Booting`、`Idle`、`BleScan`、`CameraSleepGuard`、`BleReady`、`WifiConnecting`、`HttpProbe`、`LiveViewRunning`、`ScanningCamera`、`ConnectingBle`、`CheckingCameraPower`、`CameraPowerOff`、`ActivatingWifi`、`ConnectingWifi`、`HttpProbing`、`PreviewStarting`、`PreviewRunning`、`PreviewStopped`、`Shooting`、`Disconnected`、`Error`。注意：`CameraSleepGuard` 仍在枚举和 `src/app/AppController.cpp` 的 switch 分支中出现，但在 `src/main.cpp` 的 `setCameraFlowState()` 调用点里实际使用的关机保护态是 `CameraPowerOff`——`CameraSleepGuard` 目前像是历史遗留、未见被真正赋值。状态切换由 `setCameraFlowState()` 驱动，并与 `rvf::AppController`（`src/app/AppController.*`）协同。
- **主循环已简化为一行**：`loop()`（`src/main.cpp`）现在只调用 `runAppTick()`；原来按钮/相机流程/Wi-Fi/LiveView/状态 UI 的顺序步骤已经内联到 `runAppTick()` 内部，并拆分调用 `src/app/AppController`、`src/services/*`（`BleCameraService`、`CameraPowerPolicy`、`CameraService`、`PreviewFrameBuffer`、`ShutterService`、`WifiPreviewService`）与 `src/supervisor/SystemSupervisor`。具体每 tick 的实际调用顺序需直接读 `runAppTick()` 源码确认，不能再假定和旧版一致。
- 当前没有 `include/` 和 `lib/` 目录。
- `docs/` 目录实际内容：`changelog_for_ai.md`、`codex_review_checklist.md`、`hardware.md`、`known_issues.md`、`pin_map.md`、`power_state_policy.md`、`project_overview.md`（本文件）、`ricoh_ble_protocol.md`、`test_plan.md`、`wifi_preview_flow.md`、`refactor/architecture_refactor_plan.md`。`images/FinalOutput.png`、`images/FinalOutput_EN.png` 已随 README 封面图一起删除（已过时，2026-07-09）；旧的 `hardware_setup.jpg`/`liveview_action.jpg` 更早以前就已不存在。

## 机型支持状态

| 机型 | 状态 |
| --- | --- |
| RICOH GR IV HDF | 已验证可用 |
| RICOH GR IV 系列（非 HDF） | 理论可用，仍需实机确认 |
| RICOH GR IIIx | **已验证可用**（2026-07-08/09 实机测试：完整 Wi-Fi LiveView + BLE 遥控快门，见 `env:m5stack-sticks3-gr3x`） |
| RICOH GR III（非 IIIx） | 当前不可用 / 未验证（是否与 GR IIIx 共享 GATT 布局尚未确认） |
| RICOH GR II | 当前不可用 |

## 功能模块

代码已从纯扁平结构演进为分层结构，`src/` 下同时存在旧的扁平模块和新增的分层目录：

- BLE（旧）：`src/ricoh_ble_client.*`
- BLE（新，服务层封装/转发）：`src/services/BleCameraService.*`
- BLE 重连策略：`src/ble_reconnect_policy.*`
- Wi-Fi（旧）：`src/gr_wifi.*`
- Wi-Fi/LiveView 服务层：`src/services/WifiPreviewService.*`、`src/services/PreviewFrameBuffer.*`
- HTTP/LiveView（旧）：`src/gr_api.*`
- 快门服务层：`src/services/ShutterService.*`
- 相机电源策略：`src/services/CameraPowerPolicy.*`、`src/services/CameraService.*`
- MJPEG：`src/mjpeg_stream.*`
- JPEG 解码：`src/jpeg_decoder.*`
- UI（旧）：`src/display.*`
- UI（新）：`src/ui/UiManager.*`、`src/ui/ButtonInput.*`、`src/ui/UserCommand.h`
- 按钮（旧）：`src/buttons.*`；按钮驱动（新）：`src/drivers/ButtonDriver.*`
- 显示驱动（新）：`src/drivers/DisplayDriver.*`
- 应用编排（新）：`src/app/AppController.*`、`src/app/AppState.h`、`src/app/AppFlowActions.h`
- 核心基础设施（新）：`src/core/AppConfig.*`、`src/core/AppEvent.h`、`src/core/AppMessage.h`、`src/core/Logger.*`、`src/core/PeriodicTask.h`、`src/core/Result.h`
- 板级配置（新）：`src/board/BoardConfig.*`、`src/board/StickS3Pins.h`
- 系统监督（新）：`src/supervisor/SystemSupervisor.*`
- 电源和主状态机编排：`src/main.cpp`（现通过 `runAppTick()` 驱动 `AppController` + 各 service）
- NVS profile：`src/camera_profile_store.*`
- 相机身份推导：`src/camera_identity.*`
- 配置常量：`src/config.h`

`src/main.cpp` 目前同时 `#include` 新旧两套模块（例如既 include `gr_wifi.h`/`ricoh_ble_client.h`，又 include `services/WifiPreviewService.h`/`services/BleCameraService.h`），说明这是一次**尚在进行中的分层重构**，旧文件未必都已是死代码，改动前需逐个确认调用关系，不能只看目录新旧来判断是否废弃。另见 `docs/refactor/architecture_refactor_plan.md`（重构计划文档，历史背景）。

## TODO_UNVERIFIED

- GR IV 非 HDF 机型的完整兼容性：README 标记为理论可用，需实机确认。
- GR III（非 IIIx）的等价协议、handle 和状态值：尚未有实机验证，`config.h` 中只有 `CAMERA_MODEL_GR3X`（IIIx），没有单独的 GR III（非 x）配置。
- `AppState::CameraSleepGuard` 是否已被 `CameraPowerOff` 完全取代、是否可以从枚举中移除：需要有人确认没有遗漏的赋值路径。
- 新旧并存的模块（如 `src/gr_wifi.*` vs `src/services/WifiPreviewService.*`）之间确切的调用/委托关系：需要通读 `src/main.cpp` 和各 service 源码确认，本文档未逐一验证。

## 后续 Codex 修改代码时必须注意

- 修改任何流程前先确认状态机入口和恢复路径——注意 `CameraFlowState`/`rvf::AppState` 定义在 `src/app/AppState.h`，不要假设它还是旧的 6 成员枚举。
- 涉及相机唤醒的改动必须同时读 `power_state_policy.md`。
- 涉及 BLE/Wi-Fi/UI 流程的改动，先确认是走旧扁平文件（`ricoh_ble_client.*`/`gr_wifi.*`/`display.*`）还是新 `src/services/*`/`src/ui/*` 路径，两套可能同时存在于调用链上。
