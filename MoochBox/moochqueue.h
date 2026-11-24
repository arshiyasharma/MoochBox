// The MoochBox queue (plan §3.3, §5): the real queue lives here; Spotify only
// ever gets the one song that plays next.
#pragma once
#include <Arduino.h>
#include <mutex>
#include <vector>
#include "spotify.h"
#include "sessions.h"

// Web handlers run on the network task, Spotify work runs in loop().
// Anything touching sessions or the queue holds this lock.
extern std::recursive_mutex stateMutex;
#define STATE_LOCK std::lock_guard<std::recursive_mutex> stateLock_(stateMutex)

enum ItemState { ITEM_WAITING, ITEM_LOCKED };  // locked = sent to Spotify, can't change

struct QueueItem {
  uint32_t id = 0;
  Track track;
  String addedBy;      // display name
  String ownerToken;   // session that added it (for "my songs" and limits)
  ItemState state = ITEM_WAITING;
  uint32_t lockedAt = 0;
  uint8_t pushTries = 0;
};

struct QueueRules {
  int perPersonLimit = 3;        // songs waiting per person, freed when one plays
  uint32_t spamGapMs = 5000;     // between adds from one person
  uint32_t lockAtMs = 15000;     // lock + send next song this long before the end
  bool addingLocked = false;     // owner/host "lock adding"
  bool voteRemoveOn = true;
  bool voteSkipOn = true;
  int votePct = 50;              // % of everyone in the room
  int voteMin = 3;               // ...but at least this many votes
};

QueueRules& queueRules();
std::vector<QueueItem>& queueItems();
int queueCountFor(const String& token);  // songs this person has waiting or locked

bool queueAdd(Session& s, const Track& t, String& message);
bool queueRemove(const Session& s, uint32_t id, String& message);
bool queueSwap(const Session& s, uint32_t id, const Track& t, String& message);

QueueItem* queueTopWaiting();   // next song to lock
QueueItem* queueLocked();       // song already sent to Spotify, if any
void queueErase(uint32_t id);
void queueRemoveAllFor(const String& token);  // kicked person's waiting songs
