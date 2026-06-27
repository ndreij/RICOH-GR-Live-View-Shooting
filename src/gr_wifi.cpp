#include "gr_wifi.h"

#include "config.h"

namespace {
constexpr uint32_t kReconnectIntervalMs = 5000;

String wifiStatusToString(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "IDLE";
    case WL_NO_SSID_AVAIL:
      return "NO_SSID";
    case WL_SCAN_COMPLETED:
      return "SCAN_DONE";
    case WL_CONNECTED:
      return "CONNECTED";
    case WL_CONNECT_FAILED:
      return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "DISCONNECTED";
    default:
      return String("UNKNOWN(") + static_cast<int>(status) + ")";
  }
}
}  // namespace

void GrWifi::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  _lastStatus = WiFi.status();
}

bool GrWifi::connect(uint32_t timeoutMs) {
  if (isConnected()) {
    return true;
  }

  return connect(GR_WIFI_SSID, GR_WIFI_PASSWORD, timeoutMs);
}

bool GrWifi::connect(const char* ssid, const char* password, uint32_t timeoutMs) {
  if (isConnected() && ssid != nullptr && WiFi.SSID() == ssid) {
    return true;
  }

  if (connectTo(ssid, password, timeoutMs)) {
    return true;
  }

  // Some GR bodies are configured to use the SSID itself as the WPA password.
  // Keep this as a fallback only, so an intentionally empty password still gets
  // tried first for cameras running an open AP.
  if ((password == nullptr || password[0] == '\0') && ssid != nullptr && ssid[0] != '\0') {
    return connectTo(ssid, ssid, timeoutMs);
  }

  return false;
}
void GrWifi::loop() {
  const wl_status_t status = WiFi.status();
  _lastStatus = status;

  if (status == WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  if (now - _lastReconnectAttemptMs >= kReconnectIntervalMs) {
    _lastReconnectAttemptMs = now;
    WiFi.reconnect();
  }
}

bool GrWifi::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void GrWifi::disconnect() {
  WiFi.disconnect(false, false);
  _lastStatus = WiFi.status();
}

String GrWifi::ssid() const {
  return isConnected() ? WiFi.SSID() : String(GR_WIFI_SSID);
}

int32_t GrWifi::rssi() const {
  return isConnected() ? WiFi.RSSI() : 0;
}

String GrWifi::localIPString() const {
  return isConnected() ? WiFi.localIP().toString() : String("-");
}

String GrWifi::statusText() const {
  return wifiStatusToString(WiFi.status());
}

bool GrWifi::connectWithPassword(const char* password, uint32_t timeoutMs) {
  return connectTo(GR_WIFI_SSID, password, timeoutMs);
}

bool GrWifi::connectTo(const char* ssid, const char* password, uint32_t timeoutMs) {
  if (ssid == nullptr || ssid[0] == '\0') {
    _lastStatus = WL_NO_SSID_AVAIL;
    return false;
  }

  WiFi.disconnect(false, false);
  delay(100);

  if (password != nullptr && password[0] != '\0') {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  const uint32_t startMs = millis();
  while (millis() - startMs < timeoutMs) {
    const wl_status_t status = WiFi.status();
    _lastStatus = status;
    if (status == WL_CONNECTED) {
      return true;
    }
    yield();
    delay(100);
  }

  _lastStatus = WiFi.status();
  return false;
}
