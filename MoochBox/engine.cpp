#include "engine.h"
#include "moochqueue.h"
#include "net.h"
#include "leds.h"
#include "votes.h"

static const uint32_t POLL_MS = 3000;
static const uint32_t PUSH_RETRY_MS = 2000;
static const uint32_t LOCKED_TIMEOUT_MS = 15UL * 60 * 1000;  // locked song never played
static const uint32_t SHORT_SONG_MS = 30000;                 // push right away for these

// ---------- search jobs ----------

struct SearchJob {
  int id = 0;
  JobStatus status = JOB_NONE;
  String query;
  Track results[SEARCH_RESULTS];
  int count = 0;
  String error;
};
static SearchJob jobs[4];
static int nextJobId = 1;

// Songs MoochBox has shown in search results: phones can only add these.
static Track cache[40];
static int cacheNext = 0;

// ---------- playback state ----------

static BoxStatus status = BOX_STARTING;
static NowPlaying now;
static String nowAddedBy;
static String nowAddedByToken;
static EngineCommand pendingCmd = CMD_NONE;
static String lastUri;
static uint32_t lastPollAt = 0;
static bool havePoll = false;
static uint32_t lastPushTry = 0;

const char* boxStatusName(BoxStatus s) {
  switch (s) {
    case BOX_OK:        return "ok";
    case BOX_NO_DEVICE: return "no_device";
    case BOX_OFFLINE:   return "offline";
    case BOX_AUTH:      return "auth";
    default:            return "starting";
  }
}

static void setStatus(BoxStatus s) {
  status = s;
  switch (s) {
    case BOX_OK:        ledsSet(LED_OK); break;
    case BOX_NO_DEVICE: ledsSet(LED_NO_DEVICE); break;
    case BOX_STARTING:  ledsSet(LED_CONNECTING); break;
    default:            ledsSet(LED_ERROR); break;
  }
}

static void statusFromResult(SpotifyResult r) {
  switch (r) {
    case SPOTIFY_OK:            break;
    case SPOTIFY_NO_DEVICE:     setStatus(BOX_NO_DEVICE); break;
    case SPOTIFY_AUTH_FAILED:   setStatus(BOX_AUTH); break;
    case SPOTIFY_NETWORK_ERROR: setStatus(BOX_OFFLINE); break;
    default:                    break;  // rate limit / odd errors: keep the last status
  }
}

void engineBegin() { setStatus(BOX_STARTING); }

int engineStartSearch(const String& query) {
  // Reuse a finished or empty slot; refuse if every slot is still pending.
  SearchJob* slot = nullptr;
  for (SearchJob& j : jobs) {
    if (j.status != JOB_PENDING && (!slot || j.id < slot->id)) slot = &j;
  }
  if (!slot) return -1;
  *slot = SearchJob();
  slot->id = nextJobId++;
  slot->status = JOB_PENDING;
  slot->query = query;
  return slot->id;
}

JobStatus engineSearchResult(int job, Track* out, int& count, String& error) {
  for (SearchJob& j : jobs) {
    if (j.id != job) continue;
    count = j.count;
    error = j.error;
    for (int i = 0; i < j.count; i++) out[i] = j.results[i];
    return j.status;
  }
  return JOB_NONE;
}

bool engineCachedTrack(const String& uri, Track& out) {
  for (const Track& t : cache) {
    if (t.uri == uri) {
      out = t;
      return true;
    }
  }
  return false;
}

EngineView engineView() {
  EngineView v;
  v.status = status;
  v.now = now;
  v.nowAddedBy = nowAddedBy;
  v.nowAdderToken = nowAddedByToken;
  v.polledAgoMs = havePoll ? millis() - lastPollAt : 0;
  v.msUntilLock = -1;
  if (havePoll && now.active && now.isPlaying && !queueLocked() && queueTopWaiting()) {
    int32_t untilEnd = (int32_t)now.track.durationMs - (int32_t)now.progressMs - (int32_t)v.polledAgoMs;
    v.msUntilLock = max<int32_t>(0, untilEnd - (int32_t)queueRules().lockAtMs);
  }
  return v;
}

// ---------- work, one Spotify call per loop pass ----------

static bool runOneSearch() {
  String query;
  int id = 0;
  {
    STATE_LOCK;
    for (SearchJob& j : jobs) {
      if (j.status == JOB_PENDING && (!id || j.id < id)) {
        id = j.id;
        query = j.query;
      }
    }
  }
  if (!id) return false;

  Track found[SEARCH_RESULTS];
  int count = 0;
  SpotifyResult r = spotifySearch(query, found, SEARCH_RESULTS, count);  // slow, unlocked

  STATE_LOCK;
  statusFromResult(r);
  for (SearchJob& j : jobs) {
    if (j.id != id) continue;
    if (r != SPOTIFY_OK) {
      j.status = JOB_ERROR;
      j.error = spotifyResultText(r);
      break;
    }
    // Hide repeats of the same song (single vs album versions).
    for (int i = 0; i < count; i++) {
      bool dup = false;
      for (int k = 0; k < j.count; k++) {
        dup = dup || (j.results[k].title.equalsIgnoreCase(found[i].title) &&
                      j.results[k].artist.equalsIgnoreCase(found[i].artist));
      }
      if (dup) continue;
      j.results[j.count++] = found[i];
      cache[cacheNext] = found[i];
      cacheNext = (cacheNext + 1) % 40;
    }
    j.status = JOB_DONE;
    break;
  }
  return true;
}

// Lock + send the top song when the current one has ~15 s left (plan §3.4).
static bool pushIfDue() {
  String uri;
  uint32_t id = 0;
  {
    STATE_LOCK;
    if (!havePoll || !now.active || !now.isPlaying) return false;

    QueueItem* locked = queueLocked();
    if (locked) {
      // Retry a failed push once, after 2 s.
      if (locked->pushTries == 0 || locked->pushTries >= 2) return false;
      if (millis() - lastPushTry < PUSH_RETRY_MS) return false;
      uri = locked->track.uri;
      id = locked->id;
    } else {
      QueueItem* top = queueTopWaiting();
      if (!top) return false;
      uint32_t elapsed = millis() - lastPollAt;
      int32_t untilEnd = (int32_t)now.track.durationMs - (int32_t)now.progressMs - (int32_t)elapsed;
      bool due = untilEnd <= (int32_t)queueRules().lockAtMs || now.track.durationMs < SHORT_SONG_MS;
      if (!due) return false;
      top->state = ITEM_LOCKED;  // everyone sees 🔒 right away
      top->lockedAt = millis();
      top->pushTries = 0;
      uri = top->track.uri;
      id = top->id;
    }
  }

  lastPushTry = millis();
  SpotifyResult r = spotifyAddToQueue(uri);  // slow, unlocked

  STATE_LOCK;
  statusFromResult(r);
  for (QueueItem& it : queueItems()) {
    if (it.id != id) continue;
    if (r == SPOTIFY_OK) {
      it.pushTries = 99;  // sent
      Serial.printf("🔒 Sent to Spotify: %s · %s (%s)\n", it.track.title.c_str(),
                    it.track.artist.c_str(), it.addedBy.c_str());
      ledsFlashAdded();
    } else if (++it.pushTries >= 2) {
      it.state = ITEM_WAITING;  // give up for now: back in line, try at the next song
      it.pushTries = 0;
      Serial.printf("✗ Couldn't send %s to Spotify: %s\n", it.track.title.c_str(), spotifyResultText(r));
    }
    break;
  }
  return true;
}

static bool pollIfDue() {
  if (havePoll && millis() - lastPollAt < POLL_MS) return false;
  if (!netIsConnected()) {
    STATE_LOCK;
    setStatus(BOX_OFFLINE);
    lastPollAt = millis();
    return false;
  }

  NowPlaying np;
  SpotifyResult r = spotifyNowPlaying(np);  // slow, unlocked

  STATE_LOCK;
  lastPollAt = millis();
  if (r != SPOTIFY_OK) {
    statusFromResult(r);
    return true;
  }
  havePoll = true;
  now = np;
  setStatus(np.active ? BOX_OK : BOX_NO_DEVICE);

  if (np.active && np.track.uri != lastUri) {
    lastUri = np.track.uri;
    nowAddedBy = "";
    nowAddedByToken = "";
    // The locked song started: it's done, and its adder gets the slot back.
    QueueItem* locked = queueLocked();
    if (locked && locked->track.uri == np.track.uri) {
      nowAddedBy = locked->addedBy;
      nowAddedByToken = locked->ownerToken;
      Serial.printf("▶ Now playing (from %s): %s\n", locked->addedBy.c_str(), locked->track.title.c_str());
      queueErase(locked->id);
    }
  }

  // A locked song that never plays (owner skipped past it) frees its slot.
  QueueItem* locked = queueLocked();
  if (locked && millis() - locked->lockedAt > LOCKED_TIMEOUT_MS) queueErase(locked->id);
  votesTick(np.active ? np.track.uri : String());
  return true;
}

void engineRequest(EngineCommand cmd) { pendingCmd = cmd; }

// Skip / pause / play from the panel or a passed skip vote.
static bool runCommand() {
  EngineCommand cmd;
  String lockUri;
  uint32_t lockId = 0;
  {
    STATE_LOCK;
    cmd = pendingCmd;
    pendingCmd = CMD_NONE;
    if (cmd == CMD_NONE) return false;
    // Skipping before the next song is locked would let Spotify autoplay a
    // random song, so lock + send the top song first (plan §6.6).
    if (cmd == CMD_SKIP && !queueLocked()) {
      QueueItem* top = queueTopWaiting();
      if (top) {
        top->state = ITEM_LOCKED;
        top->lockedAt = millis();
        top->pushTries = 99;
        lockUri = top->track.uri;
        lockId = top->id;
      }
    }
  }

  if (lockId) {
    SpotifyResult r = spotifyAddToQueue(lockUri);
    if (r != SPOTIFY_OK) {
      STATE_LOCK;
      statusFromResult(r);
      for (QueueItem& it : queueItems()) {
        if (it.id == lockId) { it.state = ITEM_WAITING; it.pushTries = 0; }
      }
      return true;  // don't skip into autoplay
    }
  }

  SpotifyResult r = cmd == CMD_SKIP ? spotifySkip() : cmd == CMD_PAUSE ? spotifyPause() : spotifyPlay();
  STATE_LOCK;
  statusFromResult(r);
  Serial.printf("%s %s\n", cmd == CMD_SKIP ? "⏭ Skip" : cmd == CMD_PAUSE ? "⏸ Pause" : "▶ Play",
                r == SPOTIFY_OK ? "done" : spotifyResultText(r));
  lastPollAt = millis() - POLL_MS + 800;  // poll again in ~1 s to show the change
  return true;
}

void engineLoop() {
  // Order matters: commands, then the lock deadline, then phones waiting on search.
  if (runCommand()) return;
  if (pushIfDue()) return;
  if (runOneSearch()) return;
  pollIfDue();
}
