// Runs all Spotify work from loop(), one request at a time (plan §6.5):
// search jobs from phones, polling now playing, and lock + send 15 s before
// the current song ends. Web handlers never call Spotify directly.
#pragma once
#include <Arduino.h>
#include "spotify.h"

static const int SEARCH_RESULTS = 8;

enum JobStatus { JOB_NONE, JOB_PENDING, JOB_DONE, JOB_ERROR };

enum BoxStatus {
  BOX_STARTING,
  BOX_OK,
  BOX_NO_DEVICE,   // owner needs to open Spotify and press play
  BOX_OFFLINE,     // hotspot / internet down
  BOX_AUTH,        // Spotify login broken
};

struct EngineView {             // snapshot for the phone page (caller holds STATE_LOCK)
  BoxStatus status;
  NowPlaying now;
  String nowAddedBy;            // who added the playing song ("" = not from MoochBox)
  String nowAdderToken;         // their session (for "can't vote to skip your own")
  uint32_t polledAgoMs;         // to move the progress bar forward on the phone
  int32_t msUntilLock;          // < 0: no lock coming
};

const char* boxStatusName(BoxStatus s);

void engineBegin();
void engineLoop();

// Phone search: start a job, then poll it. Caller holds STATE_LOCK.
int engineStartSearch(const String& query);  // job id, or -1 if too busy
JobStatus engineSearchResult(int job, Track* out, int& count, String& error);
bool engineCachedTrack(const String& uri, Track& out);  // only songs MoochBox has seen

EngineView engineView();

// Owner/host controls and passed skip votes. Run on the next loop pass. Caller holds STATE_LOCK.
enum EngineCommand { CMD_NONE, CMD_SKIP, CMD_PAUSE, CMD_PLAY };
void engineRequest(EngineCommand cmd);
