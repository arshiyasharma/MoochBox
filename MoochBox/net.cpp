#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "net.h"
#include "leds.h"
#include "secrets.h"

const IPAddress MOOCHBOX_IP(192, 168, 4, 1);
static const char* AP_SSID = "MoochBox";
static const int AP_MAX_CLIENTS = 10;

static DNSServer dns;
static bool apStarted = false;

// Once the access point is up, every mode change must keep it (AP + station).
static wifi_mode_t stationMode() { return apStarted ? WIFI_AP_STA : WIFI_STA; }

void netScanAndPrint() {
  WiFi.mode(stationMode());
  Serial.println("Scanning for WiFi networks...");
  int count = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < count; i++) {
    String ssid = WiFi.SSID(i);
    bool match = (ssid == HOTSPOT_SSID);
    found = found || match;
    Serial.printf("  %s \"%s\"  (signal %d dBm)\n", match ? "->" : "  ", ssid.c_str(), (int)WiFi.RSSI(i));
  }
  if (count <= 0) Serial.println("  (no networks found)");
  if (!found) {
    Serial.printf("! Hotspot \"%s\" not in the list.\n", HOTSPOT_SSID);
    Serial.println("  - Is Personal Hotspot on, with Maximize Compatibility (2.4 GHz)?");
    Serial.println("  - Keep the hotspot settings screen open on the iPhone while it connects.");
    Serial.println("  - If the name above is spelled differently, copy it into");
    Serial.println("    MOOCHBOX_HOTSPOT_SSID in .env and rerun: python3 tools/make_secrets.py");
  }
  WiFi.scanDelete();
}

bool netConnectHotspot(unsigned long timeoutMs) {
  ledsSet(LED_CONNECTING);
  WiFi.mode(stationMode());
  WiFi.setSleep(false);  // AP + station needs the radio awake to stay responsive
  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  Serial.printf("Connecting to \"%s\"", HOTSPOT_SSID);

  unsigned long start = millis();
  unsigned long lastDot = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    ledsUpdate();
    if (apStarted) dns.processNextRequest();
    if (millis() - lastDot >= 1000) {
      Serial.print(".");
      lastDot = millis();
    }
    delay(20);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("✗ Couldn't join the hotspot. Check the name/password in .env.");
    ledsSet(LED_ERROR);
    return false;
  }
  Serial.printf("✓ Online. IP %s\n", WiFi.localIP().toString().c_str());
  return true;
}

bool netIsConnected() { return WiFi.status() == WL_CONNECTED; }

void netStartAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(MOOCHBOX_IP, MOOCHBOX_IP, IPAddress(255, 255, 255, 0));

  // The ESP32 has one radio, so the access point must use the hotspot's channel.
  int channel = netIsConnected() ? WiFi.channel() : 1;
  if (!WiFi.softAP(AP_SSID, MOOCHBOX_WIFI_PASSWORD, channel, 0, AP_MAX_CLIENTS)) {
    Serial.println("✗ Couldn't start the MoochBox WiFi.");
    ledsSet(LED_ERROR);
    return;
  }

  // Captive portal: answer every DNS name with our own IP.
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", MOOCHBOX_IP);
  apStarted = true;
  Serial.printf("✓ MoochBox WiFi up: \"%s\" on channel %d, page at http://%s/\n",
                AP_SSID, channel, MOOCHBOX_IP.toString().c_str());
}

void netLoop() {
  if (apStarted) dns.processNextRequest();
}
