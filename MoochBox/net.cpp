#include <Arduino.h>
#include <WiFi.h>
#include "net.h"
#include "leds.h"
#include "secrets.h"

void netScanAndPrint() {
  WiFi.mode(WIFI_STA);
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
    Serial.println("  - If the name above is spelled differently (e.g. a curly apostrophe), copy it");
    Serial.println("    into MOOCHBOX_HOTSPOT_SSID in .env and rerun: python3 tools/make_secrets.py");
  }
  WiFi.scanDelete();
}

bool netConnectHotspot(unsigned long timeoutMs) {
  ledsSet(LED_CONNECTING);
  WiFi.mode(WIFI_STA);
  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  Serial.printf("Connecting to \"%s\"", HOTSPOT_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    ledsUpdate();
    delay(50);
    if ((millis() - start) % 1000 < 50) Serial.print(".");
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
