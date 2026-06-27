#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct WifiCredential {
  String ssid;
  String passphrase;
  String cameraIp;
};

struct ShutterCommandConfig {
  String method;
  String path;
  String contentType;
  String body;
  uint32_t timeoutMs = 5000;
  bool closeLiveviewBeforeShoot = false;

  bool isValid() const {
    return method.length() > 0 && path.startsWith("/");
  }
};

struct CameraProfile {
  String cameraName;
  String bleAddress;
  WifiCredential wifi;
  ShutterCommandConfig shutter;
  uint32_t profileVersion = 2;

  bool hasWifi() const {
    return wifi.ssid.length() > 0;
  }
};

class CameraProfileStore {
public:
  bool begin();
  bool load(CameraProfile& profile);
  bool save(const CameraProfile& profile);
  bool saveBleIdentity(const String& cameraName, const String& bleAddress);
  bool clear();

private:
  Preferences _prefs;
  bool _begun = false;
};