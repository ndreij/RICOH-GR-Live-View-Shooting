# Known Issues and Open Questions

## 已知行为/限制

1. **RICOH GR IV HDF 与 RICOH GR IIIx 已完成实机验证**：README 明确声明当前代码和协议参数已在这两款机型上完成实机验证（GR IIIx 于 2026-07-08/09 验证 LiveView + BLE 快门）；其余型号（含 GR IV 非 HDF）仍为未经实测的推断。
2. **GR III 未验证 / GR II 不可用**：GR IIIx 已支持完整 Wi-Fi LiveView + BLE 遥控快门（见 README「RICOH GR IIIx 支持」章节），但 GR III（非 IIIx）虽与 GR IIIx 同代 BLE 协议、构建理论上可用，仍未经实机验证；GR II 缺乏 BLE-first 广播唤醒和按需 Wi-Fi AP 接口，不支持。
3. **缓存 Wi-Fi 首次尝试失败可能正常**：相机 AP 刚打开时可能未准备好，固件会回读 fresh BLE Wi-Fi 参数。
4. **BLE 安全连接可能偶发失败**：历史日志出现过 NimBLE security/MTU/remote disconnect 问题，代码包含 stack reset 和重试策略。
5. **相机关机/待机误唤醒风险**：已通过 Power State + Operation Mode + `CameraPowerOff` 保护态缓解，但后续改动必须保持。注意：`CameraSleepGuard` 仍是 `src/app/AppState.h` 里定义的枚举值，但代码从未真正赋值给它——运行时实际使用、日志中会打印出来的保护态是 `CameraPowerOff`（`CAMERA_POWER_OFF`）。
6. **LiveView 卡顿风险**：Wi-Fi、JPEG 解码、屏幕刷新、buffer、delay、BLE/Wi-Fi 共存均可能影响流畅度。
7. **Button A AF 是否实际触发受相机设置影响**：代码走 Shooting Service AF 参数，但相机对焦模式/远程控制设置可能影响实际表现。
8. **PlatformIO monitor ClearCommError**：历史串口日志出现 Windows monitor `ClearCommError failed`，看起来是串口监视/重连问题，不等同于固件逻辑错误。TODO_UNVERIFIED：需更多环境日志确认。

## 技术债/观察点

- `src/CLAUDE.md` 中部分行可能描述旧行为，例如旧 shutter handle 说明；以后以当前源码和 README 为准。
- `display.*` UI 可继续优化，但不得影响 LiveView 解码与连接流程。
- `WIFI_CONNECT_TIMEOUT_MS=15000` 较长，优化需评估 BLE guard 和用户体验。

## TODO_UNVERIFIED

- 当前实机稳定 FPS。
- 长时间 LiveView 运行的内存碎片情况。
- BLE reconnect 失败码与相机状态的完整映射。
- GR IV 非 HDF 的协议兼容性。
- 额外按钮/外设的硬件可行性。

## 后续 Codex 修改代码时必须注意

- 修复 known issue 前先补实机日志模板到 `logs/`。
- 不要把偶发串口监视器错误误判为固件崩溃。
- 所有“理论可用”必须保留不确定性，直到实机验证。