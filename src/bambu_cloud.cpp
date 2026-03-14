#include "bambu_cloud.h"
#include "settings.h"
#include "config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

// ---------------------------------------------------------------------------
//  Region-aware URL helpers
// ---------------------------------------------------------------------------
const char* getBambuBroker(CloudRegion region) {
  // Bambu only has US and CN MQTT brokers — EU accounts use the US broker
  switch (region) {
    case REGION_CN: return "cn.mqtt.bambulab.com";
    default:        return "us.mqtt.bambulab.com";
  }
}

const char* getBambuApiBase(CloudRegion region) {
  switch (region) {
    case REGION_CN: return "https://api.bambulab.cn";
    default:        return "https://api.bambulab.com";
  }
}

const char* getBambuTfaDomain(CloudRegion region) {
  switch (region) {
    case REGION_CN: return "bambulab.cn";
    default:        return "bambulab.com";
  }
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

// Set common headers that mimic OrcaSlicer client
static void setSlicerHeaders(HTTPClient& http) {
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "bambu_network_agent/01.09.05.01");
  http.addHeader("X-BBL-Client-Name", "OrcaSlicer");
  http.addHeader("X-BBL-Client-Type", "slicer");
  http.addHeader("X-BBL-Client-Version", "01.09.05.51");
  http.addHeader("X-BBL-Language", "en-US");
  http.addHeader("X-BBL-OS-Type", "linux");
  http.addHeader("X-BBL-OS-Version", "6.2.0");
  http.addHeader("X-BBL-Agent-Version", "01.09.05.01");
  http.addHeader("Accept", "application/json");
}

// Make an HTTPS request.
// Returns HTTP status code, or -1 on error. Response body in `response`.
static int httpsRequest(const char* method, const char* url,
                        const char* body, const char* authToken,
                        String& response) {
  WiFiClientSecure* tls = new (std::nothrow) WiFiClientSecure();
  if (!tls) return -1;
  tls->setInsecure();
  tls->setTimeout(10);

  HTTPClient http;
  if (!http.begin(*tls, url)) {
    delete tls;
    return -1;
  }

  setSlicerHeaders(http);
  if (authToken && strlen(authToken) > 0) {
    String auth = "Bearer ";
    auth += authToken;
    http.addHeader("Authorization", auth);
  }

  int httpCode;
  if (strcmp(method, "GET") == 0) {
    httpCode = http.GET();
  } else {
    httpCode = http.POST(body ? body : "");
  }

  if (httpCode > 0) {
    response = http.getString();
  }

  http.end();
  delete tls;
  return httpCode;
}

// HTTPS POST that also captures Set-Cookie headers (for TFA endpoint).
// Returns HTTP status code. Cookie value in `cookieToken` if found.
static int httpsPostWithCookie(const char* url, const char* body,
                                String& response, String& cookieToken) {
  WiFiClientSecure* tls = new (std::nothrow) WiFiClientSecure();
  if (!tls) return -1;
  tls->setInsecure();
  tls->setTimeout(10);

  HTTPClient http;
  if (!http.begin(*tls, url)) {
    delete tls;
    return -1;
  }

  setSlicerHeaders(http);
  // Collect Set-Cookie header
  const char* cookieHeaders[] = {"Set-Cookie"};
  http.collectHeaders(cookieHeaders, 1);

  int httpCode = http.POST(body ? body : "");

  if (httpCode > 0) {
    response = http.getString();
    // Extract token from Set-Cookie header
    if (http.hasHeader("Set-Cookie")) {
      String cookie = http.header("Set-Cookie");
      // Cookie format: "token=xxx; Path=/; ..."
      int start = cookie.indexOf("token=");
      if (start >= 0) {
        start += 6; // skip "token="
        int end = cookie.indexOf(';', start);
        if (end < 0) end = cookie.length();
        cookieToken = cookie.substring(start, end);
      }
    }
  }

  http.end();
  delete tls;
  return httpCode;
}

// Base64url decode (JWT uses base64url, not standard base64)
static String base64UrlDecode(const char* input, size_t len) {
  // Convert base64url to standard base64
  String b64;
  b64.reserve(len + 4);
  for (size_t i = 0; i < len; i++) {
    char c = input[i];
    if (c == '-') c = '+';
    else if (c == '_') c = '/';
    b64 += c;
  }
  // Add padding
  while (b64.length() % 4 != 0) b64 += '=';

  // Decode
  size_t outLen = 0;
  unsigned char* decoded = nullptr;

  // Use mbedtls base64 decode (available on ESP32)
  mbedtls_base64_decode(nullptr, 0, &outLen, (const unsigned char*)b64.c_str(), b64.length());
  if (outLen == 0) return "";
  decoded = (unsigned char*)malloc(outLen + 1);
  if (!decoded) return "";
  if (mbedtls_base64_decode(decoded, outLen, &outLen, (const unsigned char*)b64.c_str(), b64.length()) != 0) {
    free(decoded);
    return "";
  }
  decoded[outLen] = '\0';
  String result((char*)decoded);
  free(decoded);
  return result;
}

// ---------------------------------------------------------------------------
//  Extract userId from JWT token
// ---------------------------------------------------------------------------
bool cloudExtractUserId(const char* token, char* userId, size_t len) {
  // JWT format: header.payload.signature
  const char* dot1 = strchr(token, '.');
  if (!dot1) return false;
  const char* payloadStart = dot1 + 1;
  const char* dot2 = strchr(payloadStart, '.');
  if (!dot2) return false;

  size_t payloadLen = dot2 - payloadStart;
  String decoded = base64UrlDecode(payloadStart, payloadLen);
  if (decoded.length() == 0) return false;

  JsonDocument doc;
  if (deserializeJson(doc, decoded)) return false;

  // Try common uid field names
  const char* uid = nullptr;
  if (doc["uid"].is<const char*>()) uid = doc["uid"];
  else if (doc["sub"].is<const char*>()) uid = doc["sub"];
  else if (doc["user_id"].is<const char*>()) uid = doc["user_id"];

  if (!uid) return false;

  snprintf(userId, len, "u_%s", uid);
  return true;
}

// Store login state between login and verify calls
static char s_tfaKey[48] = {0};
static char s_email[64] = {0};   // stored for saving after TOTP verify
static bool s_isTotpMode = false; // true = authenticator app, false = email code

// ---------------------------------------------------------------------------
//  Process login response (not used for TOTP verify)
// ---------------------------------------------------------------------------
static CloudResult processLoginResponse(int httpCode, const String& response,
                                         const char* email) {
  if (httpCode < 0) return CLOUD_NET_ERROR;

  JsonDocument doc;
  if (deserializeJson(doc, response)) return CLOUD_PARSE_ERROR;

  // Check if 2FA is required
  if (doc["loginType"].is<const char*>()) {
    const char* lt = doc["loginType"];
    if (strcmp(lt, "verifyCode") == 0 || strcmp(lt, "tfa") == 0) {
      // Save state for the verify call
      const char* tfk = doc["tfaKey"] | "";
      strlcpy(s_tfaKey, tfk, sizeof(s_tfaKey));
      strlcpy(s_email, email, sizeof(s_email));
      s_isTotpMode = (strcmp(lt, "tfa") == 0);
      Serial.printf("CLOUD: 2FA required, type=%s tfaKey=%s\n", lt, s_tfaKey);
      return CLOUD_NEED_VERIFY;
    }
  }

  // Check for error response
  if (httpCode == 400 || httpCode == 401 || httpCode == 403) {
    return CLOUD_BAD_CREDS;
  }

  // Look for token in response
  const char* accessToken = nullptr;
  if (doc["accessToken"].is<const char*>()) {
    accessToken = doc["accessToken"];
  } else if (doc["data"].is<JsonObject>() && doc["data"]["accessToken"].is<const char*>()) {
    accessToken = doc["data"]["accessToken"];
  }

  if (!accessToken || strlen(accessToken) == 0) {
    if (httpCode == 200) return CLOUD_NEED_VERIFY;
    return CLOUD_PARSE_ERROR;
  }

  // Success — save token
  saveCloudToken(accessToken);
  saveCloudEmail(email);

  char uid[32] = {0};
  cloudExtractUserId(accessToken, uid, sizeof(uid));
  Serial.printf("CLOUD: Login OK, userId=%s\n", uid);

  return CLOUD_OK;
}

// ---------------------------------------------------------------------------
//  Login with email + password
// ---------------------------------------------------------------------------
CloudResult cloudLogin(const char* email, const char* password, CloudRegion region) {
  Serial.printf("CLOUD: Login attempt for %s\n", email);

  String url = String(getBambuApiBase(region)) + "/v1/user-service/user/login";

  JsonDocument body;
  body["account"] = email;
  body["password"] = password;
  String bodyStr;
  serializeJson(body, bodyStr);

  String response;
  int httpCode = httpsRequest("POST", url.c_str(), bodyStr.c_str(), nullptr, response);

  Serial.printf("CLOUD: Login HTTP %d, len=%d\n", httpCode, response.length());
  if (httpCode == 403) {
    Serial.println("CLOUD: 403 — likely Cloudflare block. First 200 chars:");
    Serial.println(response.substring(0, 200));
  }
  return processLoginResponse(httpCode, response, email);
}

// ---------------------------------------------------------------------------
//  Verify 2FA code
// ---------------------------------------------------------------------------
CloudResult cloudVerifyCode(const char* email, const char* code, CloudRegion region) {
  Serial.printf("CLOUD: Verify 2FA for %s code=%s totp=%d\n", email, code, s_isTotpMode);

  if (s_isTotpMode) {
    // TOTP authenticator: POST to bambulab.com/api/sign-in/tfa
    // Body: {tfaKey, tfaCode} — token comes back in Set-Cookie
    char tfaUrl[64];
    snprintf(tfaUrl, sizeof(tfaUrl), "https://%s/api/sign-in/tfa", getBambuTfaDomain(region));

    JsonDocument body;
    body["tfaKey"] = s_tfaKey;
    body["tfaCode"] = code;
    String bodyStr;
    serializeJson(body, bodyStr);

    String response;
    String cookieToken;
    int httpCode = httpsPostWithCookie(tfaUrl, bodyStr.c_str(), response, cookieToken);

    Serial.printf("CLOUD: TFA HTTP %d, len=%d, cookie=%d\n",
                  httpCode, response.length(), cookieToken.length());

    if (httpCode < 0) return CLOUD_NET_ERROR;
    if (httpCode == 400 || httpCode == 401 || httpCode == 403) {
      Serial.printf("CLOUD: TFA error, body: %s\n", response.substring(0, 200).c_str());
      return CLOUD_BAD_CREDS;
    }

    // Token from cookie takes priority
    String token = cookieToken;

    // Fallback: check JSON body
    if (token.length() == 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, response)) {
        const char* at = doc["accessToken"] | (const char*)nullptr;
        if (!at) at = doc["token"] | (const char*)nullptr;
        if (at && strlen(at) > 0) token = at;
      }
    }

    if (token.length() == 0) {
      Serial.println("CLOUD: TFA succeeded but no token found");
      Serial.printf("CLOUD: Response: %s\n", response.substring(0, 300).c_str());
      return CLOUD_PARSE_ERROR;
    }

    // Success
    saveCloudToken(token.c_str());
    saveCloudEmail(s_email);

    char uid[32] = {0};
    cloudExtractUserId(token.c_str(), uid, sizeof(uid));
    Serial.printf("CLOUD: TFA OK, userId=%s\n", uid);

    return CLOUD_OK;

  } else {
    // Email verification: POST to login endpoint with {account, code}
    String url = String(getBambuApiBase(region)) + "/v1/user-service/user/login";

    JsonDocument body;
    body["account"] = email;
    body["code"] = code;
    String bodyStr;
    serializeJson(body, bodyStr);

    String response;
    int httpCode = httpsRequest("POST", url.c_str(), bodyStr.c_str(), nullptr, response);

    Serial.printf("CLOUD: Verify HTTP %d, len=%d\n", httpCode, response.length());
    return processLoginResponse(httpCode, response, email);
  }
}

// ---------------------------------------------------------------------------
//  Fetch userId from profile API (fallback for non-JWT tokens)
// ---------------------------------------------------------------------------
bool cloudFetchUserId(const char* token, char* userId, size_t len, CloudRegion region) {
  String url = String(getBambuApiBase(region)) + "/v1/user-service/my/profile";

  String response;
  int httpCode = httpsRequest("GET", url.c_str(), nullptr, token, response);

  Serial.printf("CLOUD: Profile HTTP %d, len=%d\n", httpCode, response.length());

  if (httpCode != 200) return false;

  JsonDocument doc;
  if (deserializeJson(doc, response)) return false;

  // uid can be a string ("uidStr") or a number ("uid")
  String uidStr;
  if (doc["uidStr"].is<const char*>()) uidStr = (const char*)doc["uidStr"];
  else if (doc["uid"].is<const char*>()) uidStr = (const char*)doc["uid"];
  else if (!doc["uid"].isNull()) uidStr = String((long)doc["uid"].as<long long>());

  if (uidStr.length() == 0) {
    Serial.printf("CLOUD: No uid in profile: %s\n", response.substring(0, 300).c_str());
    return false;
  }

  snprintf(userId, len, "u_%s", uidStr.c_str());
  Serial.printf("CLOUD: Got userId from profile: %s\n", userId);
  return true;
}

// ---------------------------------------------------------------------------
//  Fetch device list
// ---------------------------------------------------------------------------
int cloudFetchDevices(const char* token, CloudPrinter* out, int maxDevices, CloudRegion region) {
  String url = String(getBambuApiBase(region)) + "/v1/iot-service/api/user/bind";

  String response;
  int httpCode = httpsRequest("GET", url.c_str(), nullptr, token, response);

  Serial.printf("CLOUD: Devices HTTP %d, len=%d\n", httpCode, response.length());

  if (httpCode != 200) return 0;

  // Parse with filter for memory efficiency
  JsonDocument filter;
  filter["message"] = true;
  JsonObject df = filter["data"][0].to<JsonObject>();
  df["dev_id"] = true;
  df["name"] = true;
  df["dev_product_name"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, response, DeserializationOption::Filter(filter))) {
    Serial.println("CLOUD: Failed to parse device list");
    return 0;
  }

  JsonArray devices = doc["data"].as<JsonArray>();
  if (devices.isNull()) return 0;

  int count = 0;
  for (JsonObject dev : devices) {
    if (count >= maxDevices) break;
    strlcpy(out[count].serial, dev["dev_id"] | "", sizeof(out[count].serial));
    strlcpy(out[count].name, dev["name"] | "", sizeof(out[count].name));
    strlcpy(out[count].model, dev["dev_product_name"] | "", sizeof(out[count].model));
    count++;
  }

  Serial.printf("CLOUD: Found %d devices\n", count);
  return count;
}
