#pragma once

#include <Arduino.h>
#include <WiFi.h>

class GrWifi {
public:
  void begin();
  bool connect(uint32_t timeoutMs);
  bool connect(const char* ssid, const char* password, uint32_t timeoutMs);
  void loop();
  bool isConnected() const;
  void disconnect();

  String ssid() const;
  int32_t rssi() const;
  String localIPString() const;
  String statusText() const;

private:
  bool connectWithPassword(const char* password, uint32_t timeoutMs);
  bool connectTo(const char* ssid, const char* password, uint32_t timeoutMs);

  wl_status_t _lastStatus = WL_IDLE_STATUS;
  uint32_t _lastReconnectAttemptMs = 0;
};
