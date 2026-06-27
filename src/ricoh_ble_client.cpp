#include "ricoh_ble_client.h"

#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include "config.h"

namespace {
String toUpperCopy(String value) {
  value.toUpperCase();
  return value;
}

bool nameLooksLikeRicoh(const String& name) {
  const String upper = toUpperCopy(name);
  return upper.indexOf("RICOH") >= 0 || upper.indexOf("GR") >= 0;
}

bool addressMatches(const String& candidate, const String& preferred) {
  return preferred.length() > 0 && candidate.equalsIgnoreCase(preferred);
}

RicohBleDeviceInfo infoFromAdvertisedDevice(BLEAdvertisedDevice& device) {
  RicohBleDeviceInfo info;
  info.found = true;
  info.address = device.getAddress().toString().c_str();
  info.rssi = device.getRSSI();
  if (device.haveName()) {
    info.name = device.getName().c_str();
  }
  if (device.haveServiceUUID()) {
    info.hasPrimaryService = device.isAdvertisingService(BLEUUID(RICOH_BLE_SERVICE_UUID_PRIMARY));
    info.hasAltService = device.isAdvertisingService(BLEUUID(RICOH_BLE_SERVICE_UUID_ALT));
  }
  return info;
}

int candidateScore(const RicohBleDeviceInfo& info, const String& preferredAddress) {
  int score = 0;
  if (addressMatches(info.address, preferredAddress)) {
    score += 1000;
  }
  if (info.hasPrimaryService) {
    score += 200;
  }
  if (info.hasAltService) {
    score += 150;
  }
  if (nameLooksLikeRicoh(info.name)) {
    score += 50;
  }
  score += info.rssi;
  return score;
}
}

void RicohBleClient::begin() {
  if (_begun) {
    return;
  }
  BLEDevice::init("RICOH-StickS3");
  _begun = true;
  _lastError = "";
}

RicohBleDeviceInfo RicohBleClient::scanForCamera(const String& preferredAddress, uint32_t scanSeconds) {
  begin();

  RicohBleDeviceInfo best;
  int bestScore = -100000;

  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  Serial.printf("BLE: scanning %lu seconds, preferred='%s'\n",
                static_cast<unsigned long>(scanSeconds),
                preferredAddress.c_str());
  BLEScanResults results = scan->start(scanSeconds, false);
  const int count = results.getCount();
  Serial.printf("BLE: scan found %d devices\n", count);

  for (int i = 0; i < count; ++i) {
    BLEAdvertisedDevice device = results.getDevice(i);
    RicohBleDeviceInfo info = infoFromAdvertisedDevice(device);
    const bool serviceMatch = info.hasPrimaryService || info.hasAltService;
    const bool nameMatch = nameLooksLikeRicoh(info.name);
    const bool preferredMatch = addressMatches(info.address, preferredAddress);
    if (!serviceMatch && !nameMatch && !preferredMatch) {
      continue;
    }

    Serial.printf("BLE: candidate addr=%s rssi=%d name='%s' primary=%d alt=%d\n",
                  info.address.c_str(),
                  info.rssi,
                  info.name.c_str(),
                  info.hasPrimaryService ? 1 : 0,
                  info.hasAltService ? 1 : 0);

    const int score = candidateScore(info, preferredAddress);
    if (!best.found || score > bestScore) {
      best = info;
      bestScore = score;
    }
  }

  scan->clearResults();

  if (!best.found) {
    _lastError = "RICOH BLE not found";
    Serial.println("BLE: RICOH camera not found");
  } else {
    _lastError = "";
    Serial.printf("BLE: selected addr=%s name='%s'\n", best.address.c_str(), best.name.c_str());
  }
  return best;
}

bool RicohBleClient::connectAndDiscover(const RicohBleDeviceInfo& info, uint32_t timeoutMs) {
  begin();
  disconnect();

  if (!info.found || info.address.length() == 0) {
    _lastError = "No BLE device selected";
    return false;
  }

  BLEClient* client = BLEDevice::createClient();
  _client = client;
  Serial.printf("BLE: connecting %s\n", info.address.c_str());
  if (!client->connect(BLEAddress(info.address.c_str()))) {
    _lastError = "BLE connect failed";
    disconnect();
    return false;
  }

  _connected = true;
  const uint32_t startMs = millis();
  bool primaryFound = false;
  bool altFound = false;
  while (millis() - startMs < timeoutMs) {
    primaryFound = client->getService(BLEUUID(RICOH_BLE_SERVICE_UUID_PRIMARY)) != nullptr;
    altFound = client->getService(BLEUUID(RICOH_BLE_SERVICE_UUID_ALT)) != nullptr;
    if (primaryFound || altFound) {
      break;
    }
    delay(50);
  }

  Serial.printf("BLE: services primary=%d alt=%d\n", primaryFound ? 1 : 0, altFound ? 1 : 0);
  if (!primaryFound && !altFound) {
    _lastError = "RICOH BLE services not found";
    disconnect();
    return false;
  }

  _lastError = "";
  return true;
}

void RicohBleClient::disconnect() {
  BLEClient* client = static_cast<BLEClient*>(_client);
  if (client != nullptr) {
    if (client->isConnected()) {
      client->disconnect();
    }
    delete client;
  }
  _client = nullptr;
  _connected = false;
}

String RicohBleClient::statusText() const {
  if (_connected) {
    return "BLE_CONNECTED";
  }
  if (_lastError.length() > 0) {
    return _lastError;
  }
  return _begun ? "BLE_READY" : "BLE_IDLE";
}

const String& RicohBleClient::lastError() const {
  return _lastError;
}