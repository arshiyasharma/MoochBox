// MoochBox: shared Spotify queue on an ESP32. Full design: ../plan.md
//
// Milestone 1: Spotify from Serial Monitor (search, add to queue, now playing).
// Milestone 2: the "MoochBox" WiFi, pop-up join page, room code, owner login.
// Milestone 3: the MoochBox queue (3 per person, swap/remove, lock + send 15 s
//              before the end) and the guest page to search and add songs.
//
// Arduino IDE: Board "ESP32 Dev Module", Partition Scheme "Huge APP (3MB No OTA)".
// Before uploading: python3 tools/make_secrets.py (creates secrets.h)
//
// Serial Monitor (115200 baud, "Newline"):
//   <song name>  search · 1-5 add straight to Spotify (test) · now · code · scan · help

#if !__has_include("secrets.h")
#error "secrets.h is missing. In the project folder run: python3 tools/make_secrets.py"
#endif

#include <WiFi.h>
#include "leds.h"
#include "net.h"
#include "spotify.h"
#include "sessions.h"
#include "web.h"
#include "moochqueue.h"
#include "engine.h"

static const int MAX_RESULTS = 5;
static Track results[MAX_RESULTS];
static int resultCount = 0;
static String inputLine;

static void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  <song name>  search Spotify");
  Serial.println("  1-5          add that result straight to Spotify (testing)");
  Serial.println("  now          what's playing");
  Serial.println("  code         show the room code (owner only)");
  Serial.println("  scan         list nearby WiFi networks");
  Serial.println("  help         this list");
  Serial.println();
}

static String formatTime(uint32_t ms) {
  uint32_t s = ms / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  return buf;
}

// Sets the LEDs from a Spotify result and prints any problem.
static bool handleResult(SpotifyResult r) {
  if (r == SPOTIFY_OK) {
    ledsSet(LED_OK);
    return true;
  }
  ledsSet(r == SPOTIFY_NO_DEVICE ? LED_NO_DEVICE : LED_ERROR);
  Serial.printf("✗ %s\n", spotifyResultText(r));
  return false;
}

static void doSearch(const String& query) {
  Serial.printf("Searching \"%s\"...\n", query.c_str());
  if (!handleResult(spotifySearch(query, results, MAX_RESULTS, resultCount))) return;
  if (resultCount == 0) {
    Serial.println("No results.");
    return;
  }
  for (int i = 0; i < resultCount; i++) {
    Serial.printf("  %d. %s · %s (%s)\n", i + 1, results[i].title.c_str(),
                  results[i].artist.c_str(), formatTime(results[i].durationMs).c_str());
  }
  Serial.println("Type a number to add it to the queue.");
}

static void doAdd(int index) {
  if (index < 0 || index >= resultCount) {
    Serial.println("Search for a song first, then type its number.");
    return;
  }
  const Track& t = results[index];
  Serial.printf("Adding %s · %s...\n", t.title.c_str(), t.artist.c_str());
  if (handleResult(spotifyAddToQueue(t.uri))) {
    Serial.println("✓ Added. It's in your Spotify queue.");
    ledsFlashAdded();
  }
}

static void doNowPlaying() {
  NowPlaying np;
  if (!handleResult(spotifyNowPlaying(np))) return;
  if (!np.active) {
    Serial.println("Nothing playing. Open Spotify on the phone and press play.");
    ledsSet(LED_NO_DEVICE);
    return;
  }
  Serial.printf("%s %s · %s  %s / %s\n", np.isPlaying ? "▶" : "⏸", np.track.title.c_str(),
                np.track.artist.c_str(), formatTime(np.progressMs).c_str(),
                formatTime(np.track.durationMs).c_str());
}

static void handleLine(String line) {
  line.trim();
  if (line.isEmpty()) return;

  if (line.equalsIgnoreCase("code")) {
    STATE_LOCK;
    Serial.printf("Room code: %s · guests: %d\n", roomCode().c_str(), sessionsGuestCount());
    return;
  }
  if (!netIsConnected()) {
    Serial.println("Offline. Reconnecting to the hotspot...");
    if (!netConnectHotspot(20000)) return;
  }

  if (line.equalsIgnoreCase("help")) printHelp();
  else if (line.equalsIgnoreCase("now")) doNowPlaying();
  else if (line.equalsIgnoreCase("scan")) netScanAndPrint();
  else if (line.length() == 1 && line[0] >= '1' && line[0] <= '0' + MAX_RESULTS) doAdd(line[0] - '1');
  else doSearch(line);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  ledsBegin();
  Serial.println("\n=== MoochBox · milestone 3 ===");

  netScanAndPrint();
  bool online = netConnectHotspot(20000);

  // The room works even without internet; only Spotify needs the hotspot.
  engineBegin();
  {
    STATE_LOCK;
    sessionsBegin();
  }
  netStartAccessPoint();
  webBegin();

  spotifyBegin();
  if (online) {
    if (handleResult(spotifyEnsureToken())) doNowPlaying();
  } else {
    Serial.println("No internet: fix the hotspot, then type any command to retry.");
  }
  printHelp();
}

void loop() {
  netLoop();
  ledsUpdate();
  engineLoop();  // Spotify work for phones: search, now playing, lock + send

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputLine.length()) handleLine(inputLine);
      inputLine = "";
    } else if (inputLine.length() < 100) {
      inputLine += c;
    }
  }
}
