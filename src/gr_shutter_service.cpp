#include "gr_shutter_service.h"

#include "config.h"

bool GrShutterService::shoot(GrApi& api, const ShutterCommandConfig& config, String& message) {
  if (!config.isValid()) {
    message = "API NOT SET";
    return false;
  }

  int status = 0;
  String response;
  const uint32_t timeoutMs = config.timeoutMs > 0 ? config.timeoutMs : SHUTTER_TIMEOUT_MS;
  const bool ok = api.request(config.method,
                              config.path,
                              config.contentType,
                              config.body,
                              timeoutMs,
                              &status,
                              &response);
  if (!ok) {
    message = String("HTTP ") + status + " " + api.lastError();
    return false;
  }

  message = String("SHOT OK ") + status;
  return true;
}