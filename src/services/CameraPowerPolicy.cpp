#include "CameraPowerPolicy.h"

#include "../config.h"

namespace rvf {

bool CameraPowerPolicy::requiresPowerCheck() const {
    return RICOH_BLE_REQUIRE_POWER_ON_BEFORE_WIFI;
}

bool CameraPowerPolicy::shouldReadOperationMode(bool powerReadOk, CameraPowerStatus powerStatus) const {
    return powerReadOk && powerStatus == CameraPowerStatus::On && RICOH_BLE_BLOCK_WIFI_IN_STANDBY_OPERATION_MODE;
}

bool CameraPowerPolicy::blocksStandbyOperationMode(bool operationModeReadOk,
                                                   CameraOperationStatus operationStatus,
                                                   bool manualWakeOverride) const {
    return operationModeReadOk && operationStatus == CameraOperationStatus::Standby && !manualWakeOverride;
}

bool CameraPowerPolicy::mayActivateWifi(bool powerReadOk,
                                        CameraPowerStatus powerStatus,
                                        bool manualWakeOverride) const {
    if (powerReadOk && powerStatus == CameraPowerStatus::On) {
        return true;
    }
    if (manualWakeOverride) {
        return true;
    }
    return allowsWifiWhenPowerUnknown(powerReadOk);
}

bool CameraPowerPolicy::allowsWifiWhenPowerUnknown(bool powerReadOk) const {
    return !powerReadOk && RICOH_BLE_ALLOW_WIFI_WHEN_POWER_UNKNOWN;
}

const char* CameraPowerPolicy::blockedReason(bool powerReadOk, CameraPowerStatus powerStatus) const {
    return (powerReadOk && powerStatus == CameraPowerStatus::Off) ? "BLE power state off" : "BLE power state unknown";
}

bool CameraPowerPolicy::mayActivateWifi(CameraPowerStatus status) const {
    return status == CameraPowerStatus::On;
}

}  // namespace rvf
