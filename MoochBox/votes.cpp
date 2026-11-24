#include "votes.h"
#include "moochqueue.h"

static const uint32_t VOTE_WINDOW_MS = 60UL * 1000;
static const int MAX_VOTERS = 12;

struct Vote {
  bool used = false;
  VoteKind kind = VOTE_REMOVE;
  uint32_t itemId = 0;   // remove votes
  String uri;            // skip votes
  uint32_t voters[MAX_VOTERS];
  int count = 0;
  uint32_t startedAt = 0;
};
static Vote votes[8];

int votesNeeded() {
  const QueueRules& r = queueRules();
  int people = sessionsActiveCount();
  int byPct = (people * r.votePct + 99) / 100;  // round up
  return max(r.voteMin, byPct);
}

static bool matches(const Vote& v, VoteKind kind, uint32_t itemId, const String& uri) {
  if (!v.used || v.kind != kind) return false;
  return kind == VOTE_REMOVE ? v.itemId == itemId : v.uri == uri;
}

static Vote* findVote(VoteKind kind, uint32_t itemId, const String& uri) {
  for (Vote& v : votes) {
    if (matches(v, kind, itemId, uri) && millis() - v.startedAt < VOTE_WINDOW_MS) return &v;
  }
  return nullptr;
}

static Vote* newVote(VoteKind kind, uint32_t itemId, const String& uri) {
  Vote* slot = nullptr;
  for (Vote& v : votes) {
    if (!v.used || millis() - v.startedAt >= VOTE_WINDOW_MS) {
      slot = &v;
      break;
    }
  }
  if (!slot) return nullptr;
  *slot = Vote();
  slot->used = true;
  slot->kind = kind;
  slot->itemId = itemId;
  slot->uri = uri;
  slot->startedAt = millis();
  return slot;
}

bool votesCast(const Session& s, VoteKind kind, uint32_t itemId, const String& nowUri,
               const String& nowAdderToken, String& message, bool& passed) {
  passed = false;
  const QueueRules& rules = queueRules();

  if (kind == VOTE_REMOVE) {
    if (!rules.voteRemoveOn) { message = "Vote to remove is off."; return false; }
    QueueItem* it = nullptr;
    for (QueueItem& q : queueItems()) {
      if (q.id == itemId) it = &q;
    }
    if (!it) { message = "That song isn't in the queue anymore."; return false; }
    if (it->state == ITEM_LOCKED) { message = "That song is locked in."; return false; }
    if (it->ownerToken == s.token) { message = "That's your song: just remove it."; return false; }
  } else {
    if (!rules.voteSkipOn) { message = "Vote to skip is off."; return false; }
    if (nowUri.isEmpty()) { message = "Nothing is playing."; return false; }
    if (nowAdderToken.length() && nowAdderToken == s.token) {
      message = "You added this song, so you can't vote to skip it.";
      return false;
    }
  }

  Vote* v = findVote(kind, itemId, nowUri);
  if (!v) v = newVote(kind, itemId, nowUri);
  if (!v) { message = "Too many votes going on. Try again in a bit."; return false; }

  for (int i = 0; i < v->count; i++) {
    if (v->voters[i] == s.id) { message = "You already voted."; return false; }
  }
  if (v->count < MAX_VOTERS) v->voters[v->count++] = s.id;

  if (v->count >= votesNeeded()) {
    passed = true;
    v->used = false;
    if (kind == VOTE_REMOVE) queueErase(itemId);
  }
  return true;
}

VoteInfo votesFor(VoteKind kind, uint32_t itemId, const String& nowUri, uint32_t sessionId) {
  VoteInfo info;
  info.needed = votesNeeded();
  Vote* v = findVote(kind, itemId, nowUri);
  if (!v) return info;
  info.count = v->count;
  for (int i = 0; i < v->count; i++) info.iVoted = info.iVoted || v->voters[i] == sessionId;
  return info;
}

void votesTick(const String& nowUri) {
  for (Vote& v : votes) {
    if (!v.used) continue;
    bool expired = millis() - v.startedAt >= VOTE_WINDOW_MS;
    bool oldSong = v.kind == VOTE_SKIP && v.uri != nowUri;  // a new song resets the skip vote
    if (expired || oldSong) v.used = false;
  }
}
