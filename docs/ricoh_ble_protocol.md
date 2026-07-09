# RICOH BLE Protocol Notes

> 最近一次核对：2026-07-09（对照当前 `main` 分支 `src/config.h`、`src/ricoh_ble_client.cpp`、`src/services/BleCameraService.*` 逐项复查）。

## 适用范围

以下信息从当前代码、README 和配置常量提取。GR IV 协议以 RICOH GR IV HDF 实测为准。**GR IIIx 协议现已实机验证可用**（LiveView + BLE 快门，2026-07-08/09），其 GATT 布局与 GR IV 完全不同，见下方专门章节。GR III（非 IIIx）/ GR II 仍不可用（未验证协议）。

## BLE 服务 UUID（GR IV）

从 `src/config.h` 确认：

| 名称 | UUID |
| --- | --- |
| Info Service | `9A5ED1C5-74CC-4C50-B5B6-66A48E7CCFF1` |
| Camera Service | `4B445988-CAA0-4DD3-941D-37B4F52ACA86` |
| Operation Mode | `1452335A-EC7F-4877-B8AB-0F72E18BB295` |
| Shooting Service | `9F00F387-8345-4BBC-8B92-B87B52E3091A` |
| Shooting Flavor | `B29E6DE3-1AEC-48C1-9D05-02CEA57CE664` |
| Operation Request | `559644B8-E0BC-4011-929B-5CF9199851E7` |
| Control Service | `0F291746-0C80-4726-87A7-3C501FD3B4B6` |

## GR IV WLAN / Power handles

从 `src/config.h` 确认：

| 功能 | Handle / Value | 代码含义 |
| --- | --- | --- |
| WLAN Power | `0x0135` | 写 `0x01` 请求打开相机 Wi-Fi |
| WLAN ON value | `0x01` | `RICOH_BLE_GR4_WLAN_ON_VALUE` |
| WLAN SSID | `0x0138` | 读取 SSID |
| WLAN Passphrase | `0x013A` | 读取密码 |
| WLAN Security | `0x013C` | 读取安全类型 |
| WLAN Frequency | `0x013E` | 读取频率，并推导信道 |
| WLAN BSSID | `0x0140` | 读取或辅助解析 BSSID |
| Power State | `0x00EB` | 读取/通知相机电源状态 |
| Power State CCCD | `0x00EC` | 写 `0x01 0x00` 订阅通知 |
| Power ON value | `0x01` | 代码映射为 `RicohCameraPowerState::On` |
| Power OFF value | `0x00` | 代码映射为 `OffOrShuttingDown` |

## GR IIIx WLAN / Power handles（2026-07 新增，实机 GATT dump 验证）

GR IIIx 的 GATT 布局与 GR IV **完全不同**，没有 GR IV 的 `0x0135` 系列 WLAN 特征值。从 `src/config.h` 确认（编译期通过 `-DCAMERA_MODEL_GR3X` 选中，见下方「编译期机型选择」）：

| 功能 | Handle / Value | 代码含义 |
| --- | --- | --- |
| Network Type（充当 Wi-Fi 开关） | `0x00F0` | 特征值 `9111CDD0-...`；写 `0x01` 即把相机切到 AP 模式（无独立的 WLAN Power 概念） |
| WLAN ON value | `0x01` | `RICOH_BLE_GR3X_WLAN_ON_VALUE` |
| WLAN SSID | `0x00F3` | 特征值 `90638E5A-...`；静态可读值 |
| WLAN Passphrase | `0x00F5` | 特征值 `0F38279C-...`；静态可读值 |
| WLAN Security | 无 | GR IIIx 不提供等价特征值（`RICOH_BLE_GR3X_WLAN_SECURITY_HANDLE = 0`） |
| WLAN Frequency | 无 | 不提供 MHz 特征值；`0x00F7` 是 Channel（信道号），不是频率，代码里未使用（`RICOH_BLE_GR3X_WLAN_FREQUENCY_HANDLE = 0`） |
| WLAN BSSID | 无 | GR IIIx 不暴露 BSSID 特征值（`RICOH_BLE_GR3X_WLAN_BSSID_HANDLE = 0`） |
| Power State | `0x00BC` | 特征值 `B58CE84C-...` |
| Power State CCCD | `0x00BD` | 紧跟在 value handle 之后 |
| Power ON value | `0x01` | `RICOH_BLE_GR3X_POWER_STATE_ON_VALUE` |
| Power OFF value | `0x00` | `RICOH_BLE_GR3X_POWER_STATE_OFF_VALUE` |

Wi-Fi 打开后，GR IIIx 复用与 GR IV 相同的 STA 连接 + HTTP MJPEG LiveView 链路；快门链路（Shooting Service / Shooting Flavor / Operation Request）与 GR IV 使用同一套 UUID，未发现差异。

### 编译期机型选择

`src/config.h` 用 `#ifdef CAMERA_MODEL_GR3X` 把 `RICOH_BLE_WLAN_*` / `RICOH_BLE_POWER_STATE_*` 这组"通用宏"重定向到 `RICOH_BLE_GR3X_*` 常量；未定义该宏时（默认 `m5stack-sticks3` 环境）走 GR IV 的 `RICOH_BLE_GR4_*` 常量。也就是说同一套调用代码（`src/ricoh_ble_client.cpp`）通过这些宏在编译期切换到不同机型的 handle，而不是运行时探测。新增机型需要在这里补一组 `RICOH_BLE_<MODEL>_*` 常量 + 对应的 `#ifdef` 分支。

## Operation Mode 映射

从 `src/ricoh_ble_client.cpp::readOperationMode()` 确认（GR IV / GR IIIx 共用同一套映射）：

| Value | Mode |
| --- | --- |
| `0x00` | `CAPTURE` |
| `0x01` | `PLAYBACK` |
| `0x02` | `BLE_STARTUP` |
| `0x03` | `OTHER` |
| `0x04` | `POWER_OFF_TRANSFER` |
| other | `UNKNOWN` |

`src/main.cpp::isCameraStandbyOperationMode()` 将 `BLE_STARTUP` 和 `POWER_OFF_TRANSFER` 视为待机/关机相关状态。

## 扫描与连接

从 `src/ricoh_ble_client.cpp` 确认：

- 候选设备通过广告服务或名称判断。
- `advertisesAnyRicohService()` 匹配 Info/Camera/Shooting/Control 四个服务之一。
- `nameLooksLikeRicoh()` 接受 `GR`、`GR_`、包含 `RICOH`、`PENTAX`、`GRIII`、`GR III` 等名称特征。
- 连接后调用 `secureConnection(true)` 等待加密。
- Security wait 默认来自 `RICOH_BLE_SECURITY_WAIT_MS`，bonded 直连使用 `RICOH_BLE_BONDED_SECURITY_WAIT_MS`。

## Wi-Fi 参数读取

`waitForWifiCredentials()` 会在超时窗口内轮询 WLAN SSID/PASSPHRASE/SECURITY/FREQUENCY/BSSID handles。每个 handle 读取后有短 `delay(20)` 和 `yield()`；未得到 valid credentials 时按 `RICOH_BLE_WIFI_CREDENTIAL_POLL_MS` 延迟重试。GR IIIx 因为 Security/Frequency/BSSID handle 都是 `0`（不存在），这几项轮询在 GR IIIx 上会直接判定为不适用/跳过，只依赖 SSID + Passphrase 两个静态值。

## 快门控制

从 `src/ricoh_ble_client.cpp::shoot()` 确认：

- Shooting Service：`9F00F387-8345-4BBC-8B92-B87B52E3091A`
- 写 Shooting Flavor：`0x00`，含义为 IMMEDIATE。
- 写 Operation Request：`{0x01, param}`。
- `param=0x01` 表示 autofocus，`param=0x00` 表示 no AF。
- 当前 Button A 调用 `bleCamera.shoot(true)`。
- GR IIIx 复用同一套 UUID 和调用路径，未发现差异。

## 相机电源状态判断规则

- Wi-Fi ON 前必须先读 Power State。
- Power State `0x01` 不足以证明相机处于拍摄可用状态；还必须读取 Operation Mode。
- `BLE_STARTUP` / `POWER_OFF_TRANSFER` 下自动流程必须进入关机保护态（`CameraPowerOff`，见 `docs/project_overview.md` 关于状态机重命名的说明），不得写 WLAN ON / Network Type ON。
- Power State `0x00` 通知会触发保护态。
- 断连 reason `0x213` 或 `0x215` 会被视为 power-off/user remote disconnect 候选，并触发保护态（具体含义以当前代码常量为准）。

## 关机后重新唤醒探测（auto-probe debounce，2026-07 新增）

GR IIIx 上实测发现：相机关机后，BLE 广播里的电源位（advert manufacturer-data 的 AWAKE 位）会**保持"唤醒"状态一段时间**，不能仅凭广播位判断相机是否真的开机。为此新增了一套基于真实 GATT 读取的去抖动机制（`src/config.h` + `src/main.cpp::serviceAutoProbeIfDue()`）：

- `RICOH_BLE_AUTO_PROBE`（默认开启）：整个自动探测机制的开关。
- `RICOH_BLE_AWAKE_ADVERT_DEBOUNCE = 2`：广播位本身需要连续匹配到 2 次才会触发下一步（GATT 层面的探测），先做一轮低成本过滤。
- `RICOH_BLE_AWAKE_SCAN_WINDOW_MS = 2500`：广播扫描窗口。
- `RICOH_BLE_AUTO_PROBE_INTERVAL_MS = 300`：广播扫描轮询间隔。
- `CAMERA_POWER_OFF_PROBE_BACKOFF_MS = 8000`：保护态期间，两次真实 GATT 探测连接之间的最小间隔（避免频繁连接骚扰相机 / 耗电）。
- 每次探测都会发起一次真正的 no-security 连接，调用 `probeOperationModeNoSecurity()`（实现在 `src/ricoh_ble_client.cpp`，`src/services/BleCameraService.cpp` 里是一层透传封装）读取 Operation Mode。
- `RICOH_BLE_AUTO_PROBE_CAPTURE_CONFIRMATIONS = 2`：必须**连续 2 次**读到 `CAPTURE`（`0x00`）才认为相机真的被用户重新开机，进而调用 `requestManualCameraWake()` 唤醒完整连接流程；期间只要有一次读到非 `CAPTURE`（比如仍是 `Other`/`PowerOffTransfer`），计数器清零重新开始。这个去抖是为了过滤相机关机瞬间 BLE MCU 还没完全稳定时的误读（单次误读曾导致「Connecting…」和「Camera off」来回闪烁，见 commit `6236981`）。
- BtnA 手动唤醒不受此去抖影响，始终立即生效（安全旁路，见 `power_state_policy.md`）。

## TODO_UNVERIFIED

- UUID/handle 是否适用于所有 GR IV 非 HDF 机型。
- ~~GR III / GR II 的等价协议、handle 和状态值~~ → **GR IIIx 部分已解决**（见上方 GR IIIx 章节）；GR III（非 IIIx）和 GR II 仍未验证。
- `0x03 OTHER` 的具体相机语义。
- Passkey 的相机侧交互细节；代码中存在 passkey request 和 confirm passkey 流程，但完整 UX 需实机日志确认。
- `src/services/BleCameraService.*` 相对 `src/ricoh_ble_client.*` 的完整职责边界（目前看是部分透传封装，是否会逐步接管全部 BLE 调用尚不确定）。

## 后续 Codex 修改代码时必须注意

- 不得新增或修改 UUID/handle，除非有抓包、官方资料或实机日志证据。
- 新机型的 handle 差异一律通过 `src/config.h` 里的 `RICOH_BLE_<MODEL>_*` 常量 + 编译期宏重定向解决，不要在运行时分支里硬编码新机型的 magic number。
- 任何 BLE 重连优化不得绕过 `ensureCameraPowerReadyForWifi()`。
- BLE 回调只做轻量状态记录，禁止耗时操作。
- 涉及"相机是否已开机"的判断，优先信任 GATT 读取（Operation Mode / Power State 特征值），不要信任广播位——GR IIIx 已证明广播位在关机后会滞后。
