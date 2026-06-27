#include "gr_api.h"

#include "config.h"

namespace {
constexpr uint32_t kDefaultConnectTimeoutMs = 5000;
constexpr uint32_t kHeaderMaxBytes = 2048;
constexpr uint32_t kPropsBodyMaxBytes = 16 * 1024;

String lowerCopy(String value) {
  value.toLowerCase();
  return value;
}

int findJsonKey(const String& json, const char* key) {
  const String quotedKey = String("\"") + key + "\"";
  int pos = json.indexOf(quotedKey);
  if (pos < 0) {
    pos = json.indexOf(String(key));
  }
  if (pos < 0) {
    return -1;
  }
  return json.indexOf(':', pos);
}

bool extractJsonString(const String& json, const char* key, String& out) {
  int colon = findJsonKey(json, key);
  if (colon < 0) {
    return false;
  }

  int begin = colon + 1;
  while (begin < json.length() && isspace(static_cast<unsigned char>(json[begin]))) {
    ++begin;
  }
  if (begin >= json.length() || json[begin] != '"') {
    return false;
  }

  ++begin;
  String value;
  bool escaped = false;
  for (int i = begin; i < json.length(); ++i) {
    const char c = json[i];
    if (escaped) {
      value += c;
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      out = value;
      return true;
    }
    value += c;
  }
  return false;
}

bool extractJsonInt(const String& json, const char* key, int& out) {
  int colon = findJsonKey(json, key);
  if (colon < 0) {
    return false;
  }

  int begin = colon + 1;
  while (begin < json.length() && isspace(static_cast<unsigned char>(json[begin]))) {
    ++begin;
  }
  if (begin >= json.length()) {
    return false;
  }

  int end = begin;
  if (json[end] == '-') {
    ++end;
  }
  while (end < json.length() && isdigit(static_cast<unsigned char>(json[end]))) {
    ++end;
  }
  if (end == begin) {
    return false;
  }

  out = json.substring(begin, end).toInt();
  return true;
}

bool extractJsonBool(const String& json, const char* key, bool& out) {
  int colon = findJsonKey(json, key);
  if (colon < 0) {
    return false;
  }

  int begin = colon + 1;
  while (begin < json.length() && isspace(static_cast<unsigned char>(json[begin]))) {
    ++begin;
  }
  if (json.substring(begin, begin + 4) == "true") {
    out = true;
    return true;
  }
  if (json.substring(begin, begin + 5) == "false") {
    out = false;
    return true;
  }
  return false;
}
}  // namespace

void GrApi::setEndpoint(const char* host, uint16_t port) {
  if (host != nullptr && host[0] != '\0') {
    _host = host;
  }
  _port = port;
}

bool GrApi::fetchProps(CameraProps& props, uint32_t timeoutMs) {
  props = CameraProps{};

  WiFiClient client;
  if (!connectClient(client, timeoutMs)) {
    return false;
  }

  const String host = _host.length() ? _host : String(GR_HOST);
  client.print(String("GET /v1/props HTTP/1.1\r\nHost: ") + host + "\r\nConnection: close\r\n\r\n");

  String headers;
  if (!readHttpHeaders(client, timeoutMs, headers)) {
    client.stop();
    return false;
  }

  const int status = parseHttpStatus(headers);
  if (status != 200) {
    client.stop();
    setError(String("GET /v1/props HTTP ") + status);
    return false;
  }

  const int contentLength = parseContentLength(headers);
  String body;
  if (contentLength > 0) {
    body.reserve(contentLength + 1);
  }

  const uint32_t startMs = millis();
  while (client.connected() || client.available()) {
    while (client.available()) {
      body += static_cast<char>(client.read());
      if (body.length() > kPropsBodyMaxBytes) {
        client.stop();
        setError("/v1/props body too large");
        return false;
      }
      if (contentLength > 0 && body.length() >= contentLength) {
        client.stop();
        parsePropsJson(body, props);
        _lastError = "";
        return props.ok;
      }
    }
    if (millis() - startMs > timeoutMs) {
      client.stop();
      setError("Timed out reading /v1/props body");
      return false;
    }
    yield();
    delay(1);
  }

  client.stop();
  parsePropsJson(body, props);
  _lastError = "";
  return props.ok;
}


bool GrApi::fetchStatusDevice(String& body, uint32_t timeoutMs) {
  int status = 0;
  body = "";
  return request("GET", "/v1/status/device", "", "", timeoutMs, &status, &body) && status == 200;
}

bool GrApi::request(const String& method,
                    const String& path,
                    const String& contentType,
                    const String& body,
                    uint32_t timeoutMs,
                    int* httpStatus,
                    String* responseBody) {
  if (path.length() == 0 || path[0] != '/') {
    setError("Invalid HTTP path");
    return false;
  }

  WiFiClient client;
  if (!connectClient(client, timeoutMs)) {
    return false;
  }

  const String host = _host.length() ? _host : String(GR_HOST);
  client.print(method + " " + path + " HTTP/1.1\r\n");
  client.print(String("Host: ") + host + "\r\n");
  client.print("Connection: close\r\n");
  if (body.length() > 0) {
    client.print(String("Content-Length: ") + body.length() + "\r\n");
    if (contentType.length() > 0) {
      client.print(String("Content-Type: ") + contentType + "\r\n");
    }
  }
  client.print("\r\n");
  if (body.length() > 0) {
    client.print(body);
  }

  String headers;
  if (!readHttpHeaders(client, timeoutMs, headers)) {
    client.stop();
    return false;
  }

  const int status = parseHttpStatus(headers);
  if (httpStatus != nullptr) {
    *httpStatus = status;
  }

  const int contentLength = parseContentLength(headers);
  String response;
  if (contentLength > 0) {
    response.reserve(contentLength + 1);
  }

  const uint32_t startMs = millis();
  while (client.connected() || client.available()) {
    while (client.available()) {
      response += static_cast<char>(client.read());
      if (response.length() > kPropsBodyMaxBytes) {
        client.stop();
        setError("HTTP response body too large");
        return false;
      }
      if (contentLength > 0 && response.length() >= contentLength) {
        client.stop();
        if (responseBody != nullptr) {
          *responseBody = response;
        }
        _lastError = "";
        return status >= 200 && status < 300;
      }
    }
    if (millis() - startMs > timeoutMs) {
      client.stop();
      setError(String("Timed out reading HTTP response for ") + path);
      return false;
    }
    yield();
    delay(1);
  }

  client.stop();
  if (responseBody != nullptr) {
    *responseBody = response;
  }
  _lastError = "";
  return status >= 200 && status < 300;
}
bool GrApi::openLiveView() {
  closeLiveView();

  if (!connectClient(_liveClient, kDefaultConnectTimeoutMs)) {
    return false;
  }

  const String host = _host.length() ? _host : String(GR_HOST);
  _liveClient.print(String("GET /v1/liveview HTTP/1.1\r\nHost: ") + host +
                    "\r\nConnection: keep-alive\r\n\r\n");

  String headers;
  if (!readHttpHeaders(_liveClient, kDefaultConnectTimeoutMs, headers)) {
    _liveClient.stop();
    return false;
  }

  const int status = parseHttpStatus(headers);
  if (status != 200) {
    _liveClient.stop();
    setError(String("GET /v1/liveview HTTP ") + status);
    return false;
  }

  _liveViewOpen = true;
  _lastError = "";
  return true;
}

void GrApi::closeLiveView() {
  if (_liveClient.connected() || _liveClient.available()) {
    _liveClient.stop();
  }
  _liveViewOpen = false;
}

bool GrApi::isLiveViewOpen() {
  if (!_liveViewOpen) {
    return false;
  }
  if (!_liveClient.connected()) {
    _liveViewOpen = false;
    setError("Liveview connection closed");
    return false;
  }
  return true;
}

int GrApi::readLiveView(uint8_t* dst, size_t len) {
  if (dst == nullptr || len == 0 || !isLiveViewOpen()) {
    return 0;
  }

  size_t total = 0;
  while (total < len && _liveClient.available()) {
    const int n = _liveClient.read(dst + total, len - total);
    if (n <= 0) {
      break;
    }
    total += static_cast<size_t>(n);
  }
  return static_cast<int>(total);
}

const String& GrApi::lastError() const {
  return _lastError;
}

bool GrApi::connectClient(WiFiClient& client, uint32_t timeoutMs) {
  const String host = _host.length() ? _host : String(GR_HOST);
  const uint16_t port = _port > 0 ? _port : GR_PORT;
  client.setTimeout(timeoutMs);
  if (!client.connect(host.c_str(), port, timeoutMs)) {
    setError(String("Failed to connect to ") + host + ":" + port);
    return false;
  }
  return true;
}

bool GrApi::readHttpHeaders(WiFiClient& client, uint32_t timeoutMs, String& headers) {
  headers = "";
  headers.reserve(512);

  const uint32_t startMs = millis();
  while (millis() - startMs < timeoutMs) {
    while (client.available()) {
      const char c = static_cast<char>(client.read());
      headers += c;
      if (headers.endsWith("\r\n\r\n")) {
        return true;
      }
      if (headers.length() >= kHeaderMaxBytes) {
        setError("HTTP header too large");
        return false;
      }
    }
    if (!client.connected() && !client.available()) {
      break;
    }
    yield();
    delay(1);
  }

  setError("Timed out reading HTTP headers");
  return false;
}

int GrApi::parseHttpStatus(const String& headers) const {
  const int firstSpace = headers.indexOf(' ');
  if (firstSpace < 0) {
    return -1;
  }
  int end = headers.indexOf(' ', firstSpace + 1);
  const int lineEnd = headers.indexOf("\r\n");
  if (end < 0 || (lineEnd >= 0 && end > lineEnd)) {
    end = lineEnd >= 0 ? lineEnd : headers.length();
  }
  if (end <= firstSpace + 1) {
    return -1;
  }
  return headers.substring(firstSpace + 1, end).toInt();
}

int GrApi::parseContentLength(const String& headers) const {
  const String lower = lowerCopy(headers);
  int pos = lower.indexOf("content-length:");
  if (pos < 0) {
    return -1;
  }
  pos += 15;
  while (pos < lower.length() && isspace(static_cast<unsigned char>(lower[pos]))) {
    ++pos;
  }
  int end = pos;
  while (end < lower.length() && isdigit(static_cast<unsigned char>(lower[end]))) {
    ++end;
  }
  return lower.substring(pos, end).toInt();
}

void GrApi::parsePropsJson(const String& json, CameraProps& props) const {
  props.rawJson = json;
  props.ok = json.length() > 0;

  String stringValue;
  if (extractJsonString(json, "model", stringValue) ||
      extractJsonString(json, "modelName", stringValue) ||
      extractJsonString(json, "model_name", stringValue)) {
    props.model = stringValue;
  }

  int intValue = -1;
  if (extractJsonInt(json, "batteryLevel", intValue) ||
      extractJsonInt(json, "battery_level", intValue) ||
      extractJsonInt(json, "battery", intValue)) {
    props.batteryLevel = intValue;
  }

  if (extractJsonString(json, "batteryState", stringValue) ||
      extractJsonString(json, "battery_state", stringValue) ||
      extractJsonString(json, "battery", stringValue)) {
    props.batteryState = stringValue;
  }
  if (extractJsonString(json, "captureStatus", stringValue) ||
      extractJsonString(json, "capture_status", stringValue) ||
      extractJsonString(json, "status", stringValue)) {
    props.captureStatus = stringValue;
  }
  if (extractJsonString(json, "storageStatus", stringValue) ||
      extractJsonString(json, "storage_status", stringValue)) {
    props.storageStatus = stringValue;
  }

  bool boolValue = false;
  if (extractJsonBool(json, "liveViewAvailable", boolValue) ||
      extractJsonBool(json, "liveview", boolValue) ||
      extractJsonBool(json, "live_view", boolValue)) {
    props.liveViewAvailable = boolValue;
  } else {
    props.liveViewAvailable = json.indexOf("liveview") >= 0 || json.indexOf("liveView") >= 0;
  }

  if (props.model.isEmpty()) {
    props.model = "RICOH GR";
  }
  if (props.batteryLevel >= 0 && props.batteryState.length() > 0) {
    props.battery = String(props.batteryLevel) + "% " + props.batteryState;
  } else if (props.batteryLevel >= 0) {
    props.battery = String(props.batteryLevel) + "%";
  } else {
    props.battery = props.batteryState;
  }
}

void GrApi::setError(const String& message) {
  _lastError = message;
}
