#include "camera_profile_store.h"

namespace {
constexpr const char* kNamespace = "ricoh2";
}

bool CameraProfileStore::begin() {
  if (_begun) {
    return true;
  }
  _begun = _prefs.begin(kNamespace, false);
  return _begun;
}

bool CameraProfileStore::load(CameraProfile& profile) {
  if (!begin()) {
    return false;
  }

  profile = CameraProfile{};
  profile.profileVersion = _prefs.getUInt("proto_ver", 2);
  profile.cameraName = _prefs.getString("cam_name", "");
  profile.bleAddress = _prefs.getString("ble_addr", "");
  profile.wifi.ssid = _prefs.getString("wifi_ssid", "");
  profile.wifi.passphrase = _prefs.getString("wifi_pass", "");
  profile.wifi.cameraIp = _prefs.getString("cam_ip", "");
  profile.shutter.method = _prefs.getString("sh_method", "");
  profile.shutter.path = _prefs.getString("sh_path", "");
  profile.shutter.contentType = _prefs.getString("sh_ct", "");
  profile.shutter.body = _prefs.getString("sh_body", "");
  profile.shutter.timeoutMs = _prefs.getUInt("sh_timeout", 5000);
  profile.shutter.closeLiveviewBeforeShoot = _prefs.getBool("sh_close", false);
  return true;
}

bool CameraProfileStore::save(const CameraProfile& profile) {
  if (!begin()) {
    return false;
  }

  _prefs.putUInt("proto_ver", profile.profileVersion);
  _prefs.putString("cam_name", profile.cameraName);
  _prefs.putString("ble_addr", profile.bleAddress);
  _prefs.putString("wifi_ssid", profile.wifi.ssid);
  _prefs.putString("wifi_pass", profile.wifi.passphrase);
  _prefs.putString("cam_ip", profile.wifi.cameraIp);
  _prefs.putString("sh_method", profile.shutter.method);
  _prefs.putString("sh_path", profile.shutter.path);
  _prefs.putString("sh_ct", profile.shutter.contentType);
  _prefs.putString("sh_body", profile.shutter.body);
  _prefs.putUInt("sh_timeout", profile.shutter.timeoutMs);
  _prefs.putBool("sh_close", profile.shutter.closeLiveviewBeforeShoot);
  return true;
}

bool CameraProfileStore::saveBleIdentity(const String& cameraName, const String& bleAddress) {
  if (!begin()) {
    return false;
  }
  _prefs.putString("cam_name", cameraName);
  _prefs.putString("ble_addr", bleAddress);
  return true;
}

bool CameraProfileStore::clear() {
  if (!begin()) {
    return false;
  }
  return _prefs.clear();
}