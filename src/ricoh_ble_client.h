#pragma once

#include <Arduino.h>

struct RicohBleDeviceInfo {
  bool found = false;
  String name;
  String address;
  int rssi = 0;
  bool hasPrimaryService = false;
  bool hasAltService = false;
};

class RicohBleClient {
public:
  void begin();
  RicohBleDeviceInfo scanForCamera(const String& preferredAddress, uint32_t scanSeconds);
  bool connectAndDiscover(const RicohBleDeviceInfo& info, uint32_t timeoutMs);
  void disconnect();

  String statusText() const;
  const String& lastError() const;

private:
  bool _begun = false;
  bool _connected = false;
  String _lastError;
  void* _client = nullptr;
};