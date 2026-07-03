#pragma once

#include <Arduino.h>

namespace rvf {

enum class ErrorCode {
    Ok,
    Unknown,
    Timeout,
    InvalidState,
    BleConnectFailed,
    BleSecurityFailed,
    CameraPowerOff,
    WifiConnectFailed,
    HttpProbeFailed,
    PreviewTimeout,
    ShutterFailed,
};

inline const char* errorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok: return "Ok";
        case ErrorCode::Unknown: return "Unknown";
        case ErrorCode::Timeout: return "Timeout";
        case ErrorCode::InvalidState: return "InvalidState";
        case ErrorCode::BleConnectFailed: return "BleConnectFailed";
        case ErrorCode::BleSecurityFailed: return "BleSecurityFailed";
        case ErrorCode::CameraPowerOff: return "CameraPowerOff";
        case ErrorCode::WifiConnectFailed: return "WifiConnectFailed";
        case ErrorCode::HttpProbeFailed: return "HttpProbeFailed";
        case ErrorCode::PreviewTimeout: return "PreviewTimeout";
        case ErrorCode::ShutterFailed: return "ShutterFailed";
    }
    return "Unknown";
}

struct Result {
    ErrorCode code = ErrorCode::Ok;
    String message;

    bool ok() const { return code == ErrorCode::Ok; }
    bool failed() const { return !ok(); }
    const char* codeName() const { return errorCodeName(code); }

    static Result success() { return Result{}; }
    static Result failure(ErrorCode error, const String& detail = String()) {
        Result result;
        result.code = error;
        result.message = detail;
        return result;
    }
};

}  // namespace rvf
