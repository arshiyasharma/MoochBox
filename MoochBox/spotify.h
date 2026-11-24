// Spotify Web API client (plan §10.5). One request at a time.
#pragma once
#include <Arduino.h>

struct Track {
  String uri;
  String title;
  String artist;
  uint32_t durationMs = 0;
};

struct NowPlaying {
  bool active = false;     // false = nothing playing / no active device
  bool isPlaying = false;
  Track track;
  uint32_t progressMs = 0;
};

enum SpotifyResult {
  SPOTIFY_OK,
  SPOTIFY_NO_DEVICE,      // owner needs to open Spotify and press play
  SPOTIFY_AUTH_FAILED,    // refresh token bad: re-run tools/get_token.py
  SPOTIFY_FORBIDDEN,      // Premium / allowlist problem
  SPOTIFY_RATE_LIMITED,
  SPOTIFY_NETWORK_ERROR,
  SPOTIFY_OTHER_ERROR,
};

const char* spotifyResultText(SpotifyResult result);
void spotifyBegin();
SpotifyResult spotifyEnsureToken();  // refresh the access token if needed

// Fills `out` with up to `maxResults` tracks; sets `count`.
SpotifyResult spotifySearch(const String& query, Track* out, int maxResults, int& count);
SpotifyResult spotifyAddToQueue(const String& uri);
SpotifyResult spotifyNowPlaying(NowPlaying& out);
SpotifyResult spotifySkip();
SpotifyResult spotifyPause();
SpotifyResult spotifyPlay();
