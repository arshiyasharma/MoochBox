#include "moochqueue.h"

std::recursive_mutex stateMutex;

static const size_t MAX_ITEMS = 30;
static std::vector<QueueItem> items;
static QueueRules rules;
static uint32_t nextId = 1;

QueueRules& queueRules() { return rules; }
std::vector<QueueItem>& queueItems() { return items; }

int queueCountFor(const String& token) {
  int n = 0;
  for (const QueueItem& it : items) {
    if (it.ownerToken == token) n++;
  }
  return n;
}

static QueueItem* findItem(uint32_t id) {
  for (QueueItem& it : items) {
    if (it.id == id) return &it;
  }
  return nullptr;
}

// Own songs: always. Anyone's songs: DJ, host, owner. Locked songs: nobody.
static bool canEdit(const Session& s, const QueueItem& it, String& message) {
  if (it.state == ITEM_LOCKED) {
    message = "That song is locked in: it plays next.";
    return false;
  }
  if (it.ownerToken == s.token || s.role >= ROLE_DJ) return true;
  message = "You can only change your own songs.";
  return false;
}

bool queueAdd(Session& s, const Track& t, String& message) {
  if (rules.addingLocked && s.role < ROLE_HOST) {
    message = "Adding songs is locked right now.";
    return false;
  }
  if (rules.perPersonLimit > 0 && queueCountFor(s.token) >= rules.perPersonLimit) {
    message = "You have " + String(rules.perPersonLimit) +
              " songs waiting. Add more once one starts playing.";
    return false;
  }
  if (s.lastAddAt && millis() - s.lastAddAt < rules.spamGapMs) {
    uint32_t wait = (rules.spamGapMs - (millis() - s.lastAddAt) + 999) / 1000;
    message = "Slow down: you can add again in " + String(wait) + " s.";
    return false;
  }
  if (items.size() >= MAX_ITEMS) {
    message = "The queue is full.";
    return false;
  }

  QueueItem it;
  it.id = nextId++;
  it.track = t;
  it.addedBy = s.name;
  it.ownerToken = s.token;
  items.push_back(it);
  s.lastAddAt = millis();
  return true;
}

bool queueRemove(const Session& s, uint32_t id, String& message) {
  QueueItem* it = findItem(id);
  if (!it) {
    message = "That song isn't in the queue anymore.";
    return false;
  }
  if (!canEdit(s, *it, message)) return false;
  queueErase(id);
  return true;
}

bool queueSwap(const Session& s, uint32_t id, const Track& t, String& message) {
  QueueItem* it = findItem(id);
  if (!it) {
    message = "That song isn't in the queue anymore.";
    return false;
  }
  if (!canEdit(s, *it, message)) return false;
  it->track = t;  // same id and position: the new song keeps its place
  return true;
}

QueueItem* queueTopWaiting() {
  for (QueueItem& it : items) {
    if (it.state == ITEM_WAITING) return &it;
  }
  return nullptr;
}

QueueItem* queueLocked() {
  for (QueueItem& it : items) {
    if (it.state == ITEM_LOCKED) return &it;
  }
  return nullptr;
}

void queueErase(uint32_t id) {
  for (size_t i = 0; i < items.size(); i++) {
    if (items[i].id == id) {
      items.erase(items.begin() + i);
      return;
    }
  }
}

void queueRemoveAllFor(const String& token) {
  for (size_t i = items.size(); i-- > 0;) {
    if (items[i].ownerToken == token && items[i].state == ITEM_WAITING) items.erase(items.begin() + i);
  }
}
