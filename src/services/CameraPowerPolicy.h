#pragma once

namespace rvf {

enum class CameraPowerStatus {
    Unknown,
    Off,
    On,
};

enum class CameraOperationStatus {
    Unknown,
    Ready,
    Standby,
};

class CameraPowerPolicy {
public:
    bool requiresPowerCheck() const;
    bool shouldReadOperationMode(bool powerReadOk, CameraPowerStatus powerStatus) const;
    bool blocksStandbyOperationMode(bool operationModeReadOk,
                                    CameraOperationStatus operationStatus,
                                    bool manualWakeOverride) const;
    bool mayActivateWifi(bool powerReadOk, CameraPowerStatus powerStatus, bool manualWakeOverride) const;
    bool allowsWifiWhenPowerUnknown(bool powerReadOk) const;
    const char* blockedReason(bool powerReadOk, CameraPowerStatus powerStatus) const;

    // Backward-compatible helper from the stage-1 skeleton.
    bool mayActivateWifi(CameraPowerStatus status) const;
};

}  // namespace rvf
