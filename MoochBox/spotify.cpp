#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <base64.h>
#include "spotify.h"
#include "secrets.h"

static const char* TOKEN_URL = "https://accounts.spotify.com/api/token";
static const char* API = "https://api.spotify.com/v1";

static String accessToken;
static unsigned long tokenExpiresAt = 0;  // millis()
static String refreshToken;
static Preferences prefs;

// Spotify may hand back a new refresh token. We keep it in NVS, tagged with the
// first characters of the token in secrets.h, so running get_token.py again
// (a new secrets.h token) always wins over an old saved one.
static String urlEncode(const String& s);

static String sourceTag() { return String(SPOTIFY_REFRESH_TOKEN).substring(0, 12); }

const char* spotifyResultText(SpotifyResult result) {
  switch (result) {
    case SPOTIFY_OK:            return "OK";
    case SPOTIFY_NO_DEVICE:     return "No active Spotify device. Open Spotify on the phone and press play.";
    case SPOTIFY_AUTH_FAILED:   return "Spotify login failed. Re-run: python3 tools/get_token.py, then make_secrets.py.";
    case SPOTIFY_FORBIDDEN:     return "Spotify said forbidden (Premium needed, or account not on the app's allowlist).";
    case SPOTIFY_RATE_LIMITED:  return "Spotify rate limit. Wait a few seconds.";
    case SPOTIFY_NETWORK_ERROR: return "Couldn't reach Spotify (no internet?).";
    default:                    return "Unexpected Spotify error (see log above).";
  }
}

void spotifyBegin() {
  prefs.begin("spotify", false);
  String saved = prefs.getString("rt", "");
  if (saved.length() && prefs.getString("rt_src", "") == sourceTag()) {
    refreshToken = saved;
  } else {
    refreshToken = SPOTIFY_REFRESH_TOKEN;
  }
}

static SpotifyResult statusToResult(int code) {
  if (code >= 200 && code < 300) return SPOTIFY_OK;
  if (code == 401) return SPOTIFY_AUTH_FAILED;
  if (code == 403) return SPOTIFY_FORBIDDEN;
  if (code == 404) return SPOTIFY_NO_DEVICE;
  if (code == 429) return SPOTIFY_RATE_LIMITED;
  if (code < 0) return SPOTIFY_NETWORK_ERROR;
  return SPOTIFY_OTHER_ERROR;
}

// Reads the whole response body, then parses it. Streaming straight from the
// TLS connection cut off larger responses (search) with IncompleteInput.
static DeserializationError readJson(HTTPClient& http, JsonDocument& doc, JsonDocument* filter, const char* what) {
  String body = http.getString();
  DeserializationError err = filter
      ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
      : deserializeJson(doc, body);
  if (err) {
    Serial.printf("%s JSON error: %s (got %u bytes, expected %d)\n", what, err.c_str(),
                  (unsigned)body.length(), http.getSize());
    Serial.printf("  starts with: %.80s\n", body.c_str());
  }
  return err;
}

static SpotifyResult refreshAccessToken() {
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  if (!http.begin(client, TOKEN_URL)) return SPOTIFY_NETWORK_ERROR;

  String basic = base64::encode(String(SPOTIFY_CLIENT_ID) + ":" + SPOTIFY_CLIENT_SECRET);
  http.addHeader("Authorization", "Basic " + basic);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST("grant_type=refresh_token&refresh_token=" + urlEncode(refreshToken));

  if (code != 200) {
    Serial.printf("Token refresh failed: HTTP %d %s\n", code, code > 0 ? http.getString().c_str() : "");
    http.end();
    return code == 400 || code == 401 ? SPOTIFY_AUTH_FAILED : statusToResult(code);
  }

  JsonDocument doc;
  DeserializationError err = readJson(http, doc, nullptr, "Token");
  http.end();
  if (err) return SPOTIFY_OTHER_ERROR;

  accessToken = doc["access_token"].as<String>();
  uint32_t expiresIn = doc["expires_in"] | 3600;
  tokenExpiresAt = millis() + expiresIn * 1000UL;

  const char* newRefresh = doc["refresh_token"];
  if (newRefresh && refreshToken != newRefresh) {
    refreshToken = newRefresh;
    prefs.putString("rt", refreshToken);
    prefs.putString("rt_src", sourceTag());
  }
  Serial.println("✓ Spotify access token ready (valid 1 hour).");
  return SPOTIFY_OK;
}

SpotifyResult spotifyEnsureToken() {
  // Refresh 5 minutes early.
  if (accessToken.length() && (long)(tokenExpiresAt - millis()) > 5 * 60 * 1000L) return SPOTIFY_OK;
  return refreshAccessToken();
}

static String urlEncode(const String& s) {
  const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// Sends one API request. On 401 it refreshes the token and retries once.
// The caller must call http.end() when done.
static int apiRequest(HTTPClient& http, WiFiClientSecure& client, const char* method, const String& path) {
  for (int attempt = 0; attempt < 2; attempt++) {
    SpotifyResult tokenResult = spotifyEnsureToken();
    if (tokenResult != SPOTIFY_OK) return tokenResult == SPOTIFY_AUTH_FAILED ? 401 : -1;

    if (!http.begin(client, String(API) + path)) return -1;
    http.setTimeout(10000);  // slow hotspot: wait up to 10 s for data
    http.addHeader("Authorization", "Bearer " + accessToken);
    bool isGet = strcmp(method, "GET") == 0;
    // HTTPClient skips Content-Length for an empty body; Spotify answers 411 without it.
    if (!isGet) http.addHeader("Content-Length", "0");
    int code = isGet ? http.GET() : http.sendRequest(method, "");
    if (code == 401 && attempt == 0) {
      http.end();
      accessToken = "";  // force a refresh, then retry
      continue;
    }
    if (code == 429) Serial.printf("Rate limited. Retry-After: %s s\n", http.header("Retry-After").c_str());
    return code;
  }
  return 401;
}

SpotifyResult spotifySearch(const String& query, Track* out, int maxResults, int& count) {
  count = 0;
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  const char* keep[] = {"Retry-After"};
  http.collectHeaders(keep, 1);

  String path = "/search?type=track&limit=" + String(maxResults) + "&q=" + urlEncode(query);
  int code = apiRequest(http, client, "GET", path);
  if (code != 200) {
    if (code > 0) Serial.printf("Search failed: HTTP %d\n", code);
    http.end();
    return statusToResult(code);
  }

  // Keep only the fields we need, so the parsed result stays small.
  JsonDocument filter;
  JsonObject item = filter["tracks"]["items"][0].to<JsonObject>();
  item["name"] = true;
  item["uri"] = true;
  item["duration_ms"] = true;
  item["artists"][0]["name"] = true;

  JsonDocument doc;
  DeserializationError err = readJson(http, doc, &filter, "Search");
  http.end();
  if (err) return SPOTIFY_OTHER_ERROR;

  JsonArray items = doc["tracks"]["items"];
  for (JsonObject t : items) {
    if (count >= maxResults) break;
    out[count].title = t["name"].as<String>();
    out[count].uri = t["uri"].as<String>();
    out[count].durationMs = t["duration_ms"] | 0;
    out[count].artist = t["artists"][0]["name"].as<String>();
    count++;
  }
  return SPOTIFY_OK;
}

SpotifyResult spotifyAddToQueue(const String& uri) {
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  int code = apiRequest(http, client, "POST", "/me/player/queue?uri=" + urlEncode(uri));
  if (code < 200 || code >= 300) {
    if (code > 0) Serial.printf("Add to queue failed: HTTP %d %s\n", code, http.getString().c_str());
  }
  http.end();
  return statusToResult(code);
}

SpotifyResult spotifyNowPlaying(NowPlaying& out) {
  out = NowPlaying();
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  int code = apiRequest(http, client, "GET", "/me/player/currently-playing");
  if (code == 204) {  // nothing playing right now
    http.end();
    return SPOTIFY_OK;
  }
  if (code != 200) {
    http.end();
    return statusToResult(code);
  }

  JsonDocument filter;
  filter["is_playing"] = true;
  filter["progress_ms"] = true;
  filter["item"]["name"] = true;
  filter["item"]["uri"] = true;
  filter["item"]["duration_ms"] = true;
  filter["item"]["artists"][0]["name"] = true;

  JsonDocument doc;
  DeserializationError err = readJson(http, doc, &filter, "Now playing");
  http.end();
  if (err) return SPOTIFY_OTHER_ERROR;

  out.active = !doc["item"].isNull();
  out.isPlaying = doc["is_playing"] | false;
  out.progressMs = doc["progress_ms"] | 0;
  out.track.title = doc["item"]["name"].as<String>();
  out.track.uri = doc["item"]["uri"].as<String>();
  out.track.durationMs = doc["item"]["duration_ms"] | 0;
  out.track.artist = doc["item"]["artists"][0]["name"].as<String>();
  return SPOTIFY_OK;
}

// POST/PUT with no body; any 2xx = done.
static SpotifyResult playerCommand(const char* method, const char* path) {
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  int code = apiRequest(http, client, method, path);
  if ((code < 200 || code >= 300) && code > 0) {
    Serial.printf("%s %s failed: HTTP %d %s\n", method, path, code, http.getString().c_str());
  }
  http.end();
  return statusToResult(code);
}

SpotifyResult spotifySkip() { return playerCommand("POST", "/me/player/next"); }
SpotifyResult spotifyPause() { return playerCommand("PUT", "/me/player/pause"); }
SpotifyResult spotifyPlay() { return playerCommand("PUT", "/me/player/play"); }
