# Handover: Ricoh GR IIIx BLE connectivity problem

**Purpose:** Get a fresh, independent opinion on how to detect that the camera has
been powered on and then connect **quickly and reliably** — without auto-waking the
camera. This document is self-contained; the reader has not seen our prior work.

**Status:** on hold. The firmware currently "works" but is slow/awkward. We want a
better approach or confirmation that we've hit a hardware limit.

---

## 1. The system

- **Remote device:** M5Stack StickS3 (ESP32-S3, 8 MB flash / 8 MB PSRAM), 240×135 LCD.
- **Firmware:** PlatformIO + Arduino framework. BLE stack = **NimBLE-Arduino 2.5.0**.
- **Camera:** Ricoh **GR IIIx** (BLE name `GR_D48152`, addr `04:c4:61:d4:81:52`,
  Wi-Fi AP SSID `GR_CF1386`).
- **Goal of the product:** StickS3 acts as a wireless viewfinder + shutter remote.
  Flow: connect to camera over BLE → over BLE, switch the camera into Wi-Fi AP mode
  and read dynamic Wi-Fi credentials → join the camera's Wi-Fi → pull the MJPEG
  live-view stream over HTTP and render it on the LCD. BLE shutter works in parallel.

The Wi-Fi/live-view half is solved and reliable **once a good BLE link to an
awake camera exists**. The unsolved part is the BLE connect + power-state handshake.

---

## 2. The hard safety constraint (non-negotiable)

**The StickS3 must NEVER send the "WLAN ON" / network-activation command to a camera
that is asleep/off.** On the GR IIIx, that command powers the camera up and **extends
the lens**, which is mechanically unsafe if the camera is in a bag/pocket. Therefore:

- The remote must **passively wait** until the **user** physically powers the camera on.
- Only after the camera is confirmed awake may the remote proceed to open Wi-Fi.

This rules out the "just write WLAN-ON to wake it and connect" approach that the
original GR III firmware used.

---

## 3. What we need to detect, and why it's hard

We need a reliable, ideally fast, signal for the transition **camera asleep → camera
awake (user pressed the power button)**. Everything below was measured on a real
GR IIIx in July 2026.

### BLE operation-mode characteristic
- Service UUID `4B445988-CAA0-4DD3-941D-37B4F52ACA86`,
  characteristic UUID `1452335A-EC7F-4877-B8AB-0F72E18BB295` (read by UUID on GR IIIx).
- Values: `0x00`=CAPTURE (awake, ready), `0x01`=Playback, `0x02`=BLE_STARTUP
  (BLE layer up but main camera system asleep), `0x03`=Other, `0x04`=PowerOffTransfer.

### Confirmed findings (each verified on-device)

1. **A held link latches `BLE_STARTUP`.** If we open a BLE connection while the camera
   is asleep, the operation-mode characteristic reads `0x02` and **never changes for
   the life of that connection** — even after the user powers the camera on. Re-reading
   the same characteristic on the same link keeps returning `0x02`.

2. **Operation-mode Notify never fires.** We subscribed to the characteristic's Notify
   and held the link for 161 s / 76 polls across a user power-on event. **Zero
   notifications.** So we cannot rely on a push notification for the wake event.

3. **The "Camera Power" characteristic is useless for this.** Handle `0x00BC` reads
   `0x01` ("On") whether the camera is asleep or awake. It does not distinguish the states.

4. **Only a FRESH reconnect discriminates.** A **new** BLE connection established
   *after* the user powers the camera on reads `CAPTURE (0x00)`. A new connection made
   while the camera is still asleep reads `BLE_STARTUP (0x02)`. This is the **only**
   reliable way we found to observe the power-on. I.e. you must disconnect and
   reconnect to re-sample the true state.

5. **The BLE advertisement does NOT reliably encode power state.** We dumped the
   address-matched advertisements. The camera **rotates** between two payloads roughly
   every few seconds, e.g.:
   - `mfg=5F06DA010302EC7800000300` … advertises the *control* service (last byte `00`)
   - `mfg=5F06DA010302EC7800000301` … advertises the *info* service (last byte `01`)
   Both forms appear **while the camera is asleep**, ~10 s apart, and the `…00`/control
   form reappears later in the same asleep session. So this is standard multi-UUID
   advertisement rotation, **not** a power-state flag. (We initially hoped the last
   manufacturer-data byte was a power bit; it is not.)

### The reliability problem with the fresh-reconnect approach

Because detection requires disconnect→reconnect polling, we added a backoff between
re-samples (`CAMERA_POWER_OFF_PROBE_BACKOFF_MS`). We learned:

- **At an ~8 s cadence it works:** a fresh connect made after power-on reads `CAPTURE`
  and the flow proceeds to Wi-Fi/live view. Detection latency ≈ 9 s.
- **At a 2 s cadence it BREAKS:** every fresh connect keeps reading `BLE_STARTUP`
  (`0x02`) and the link drops with **disconnect reason `534` (0x216,
  BLE_ERR_CONN_ESTABLISHMENT)**. The stick loops forever and **never** reaches
  live view, even after the user powers the camera on. Our working theory: hammering
  the camera with reconnects every ~2 s keeps its BLE subsystem perpetually restarting,
  so it never settles out of `BLE_STARTUP` into `CAPTURE`. The camera seems to need
  idle BLE time between connection attempts.

So we appear to be stuck between "poll fast = camera never wakes / churns" and
"poll slow = ~9 s latency," with no push signal and no passive advertisement signal.

### Other observed BLE disconnect reasons
- `531` (0x213) — remote user / normal remote-initiated disconnect.
- `520` (0x208) — supervision timeout.
- `534` (0x216) — connection-establishment failure; appears during the fast-reconnect churn.
- Occasional NimBLE `ble_gap_security_initiate: rc=524` on reconnect to a bonded device,
  followed by a retry.

---

## 4. Approaches tried

| # | Approach | Result |
|---|----------|--------|
| 1 | Hold one link, re-read operation mode to catch wake | ❌ latched at `0x02` forever |
| 2 | Subscribe to operation-mode Notify | ❌ never fires |
| 3 | Read "Camera Power" characteristic | ❌ always `0x01`, can't distinguish |
| 4 | Passive scan: infer power from advertisement payload | ❌ payload just rotates, no power bit |
| 5 | Periodic fresh reconnect @ 8 s, re-sample operation mode | ✅ works, ~9 s latency |
| 6 | Same but @ 2 s for lower latency | ❌ never leaves BLE_STARTUP, reason-534 churn |
| 7 | **Current:** user presses a button (BtnA) to signal "camera is on"; only then do we connect | ✅ works, near-instant, zero idle churn, but requires a manual press every session and still does one full connect+stack-reset (~3–4 s) per press |

**Current firmware behavior:** on boot (and after any power-off) the stick sits idle and
does not touch BLE at all. When the user powers the camera on and presses BtnA, the
stick re-inits its BLE stack, connects fresh, reads operation mode; if `CAPTURE` it
proceeds to Wi-Fi/live view, if still `BLE_STARTUP` it returns to the idle wait.

We are not fully happy with #7 (manual press every time; also can't auto-connect when
the camera is already on because we can't tell without connecting).

---

## 5. Open questions for the reviewer (Fable)

1. **Is there a connectionless / low-churn way to detect GR IIIx power-on** that we
   missed — e.g. a different advertisement field, a scan-response payload, a service-data
   field, TX power, or an appearance/flags change? (We only parsed the address-matched
   device's advertisement to avoid a known NimBLE heap-corruption crash when parsing
   every foreign device's payload — issue #353 class.)

2. **Why does a ~2 s reconnect cadence keep the camera in `BLE_STARTUP` while ~8 s lets
   it reach `CAPTURE`?** Is there a known GR IIIx BLE timing requirement (e.g. minimum
   idle interval, connection-parameter negotiation, a settle time after the main system
   boots) that would let us poll faster safely? What's the minimum reliable cadence?

3. **Is disconnect reason `534` (0x216) here a symptom of reconnecting too fast**, of a
   connection-parameter/MTU issue, of bonding/security re-encryption timing, or of the
   camera actively refusing? How would you distinguish these?

4. **Can we keep one link open and get the wake event another way** — e.g. subscribe to a
   *different* characteristic (not operation-mode) that DOES notify on power-on, indications
   vs notifications, or the GATT Service Changed indication? Is there a better characteristic
   to watch?

5. **If manual (button) triggering is the pragmatic answer, can we at least make each
   connect faster** — is the full NimBLE stack reset + ~3 s settle we do on each manual
   wake necessary, or can a bonded GR IIIx be reconnected more cheaply/reliably?

6. Any known Ricoh GR IIIx BLE quirks, official SDK notes, or reverse-engineering write-ups
   that bear on power-state detection and fast reconnect?

---

## 6. Reference: key code + config locations

Repo: `RICOH-GR-Live-View-Shooting`, branch `gr3x-support`. Flat C++ in `src/`.

- `src/config.h`
  - `CAMERA_POWER_OFF_PROBE_BACKOFF_MS` — reconnect re-sample cadence (currently 8000;
    2000 was proven to break connectivity, see §3).
  - `RICOH_BLE_STARTUP_IS_STANDBY = true` — treat `BLE_STARTUP` as asleep (don't send WLAN-ON).
  - GR IIIx GATT handles: Camera Power read handle `0x00BC`; Wi-Fi/network path used by
    `openWifi()` writes `0x01` to Network Type handle `0x00F0`, then reads SSID `0x00F3`
    / passphrase `0x00F5`.
- `src/ricoh_ble_client.cpp` / `.h` — NimBLE scan/connect/bonding, operation-mode read,
  Wi-Fi credential read, shutter, advertisement handling. (Also contains a compile-gated
  advertisement-dump diagnostic behind `RICOH_BLE_ADV_DUMP_DIAG`, off by default.)
- `src/main.cpp` — the flow state machine and the power-gate:
  - `ensureCameraPowerReadyForWifi()` — the gate that reads operation mode and decides
    whether to proceed or wait.
  - `isCameraStandbyOperationMode()` — classifies `BLE_STARTUP` / `PowerOffTransfer` as standby.
  - `enterCameraSleepGuard()` / `cameraSleepGuardActive()` / `cameraSleepGuardBlocksFlow()`
    — the "camera off, wait for user" state (now user-triggered; blocks auto-flow).
  - `requestManualCameraWake()` — the BtnA path: reset stack, fresh connect, run the flow.
- `src/app/AppController.cpp` — transport-agnostic flow orchestration
  (`runCameraFlowOnce`, `resumeFromBleReady`, `serviceCameraFlowIfNeeded`,
  `handleUserCommand` → routes BtnA to manual wake when the sleep guard is active).

### Build / flash / capture (for context)
- Build+flash: `platformio run -e m5stack-sticks3-gr3x -t upload --upload-port /dev/cu.usbmodem101`
- Serial is 115200 baud; captures must not reset the board mid-stream (USB CDC drops).

---

## 7. One-paragraph summary for a busy reader

We're building an ESP32-S3 BLE remote for a Ricoh GR IIIx. For safety we must never
auto-wake the camera (it extends the lens), so we must passively detect when the *user*
powers it on, then connect. We proved the camera gives us **no** usable push/passive
signal for the wake: a held BLE link latches the "asleep" state (`BLE_STARTUP`) forever,
the operation-mode Notify never fires, the "power" characteristic is always "On," and
the advertisement payload just rotates without a power bit. The **only** reliable
detector is to disconnect and make a **fresh** connection and re-read the operation mode
(`CAPTURE` = awake, `BLE_STARTUP` = asleep). But fresh-reconnect polling is a bind: at
~8 s intervals it works (≈9 s latency); at ~2 s intervals the camera never leaves
`BLE_STARTUP` and links die with reason `534`. We currently fall back to a manual button
press as the "camera is on" signal. **We want a faster, more reliable, ideally
connectionless way to detect power-on and reconnect — or confirmation that the manual
trigger is the best achievable given the hardware.**
