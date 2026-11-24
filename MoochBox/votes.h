// Votes (plan §6.6): remove someone else's waiting song, or skip the playing
// song. Passes at votePct% of everyone in the room (at least voteMin), within
// 60 s. One vote per person; you can't vote on your own song.
// Caller holds STATE_LOCK.
#pragma once
#include <Arduino.h>
#include "sessions.h"

enum VoteKind { VOTE_REMOVE, VOTE_SKIP };

struct VoteInfo {
  int count = 0;
  int needed = 0;
  bool iVoted = false;
};

int votesNeeded();

// For VOTE_REMOVE `itemId` is the queue item; for VOTE_SKIP `nowUri` is the
// playing song and `nowAdderToken` who added it ("" if not from MoochBox).
// Returns false + message if the vote isn't allowed. `passed` = it just passed
// (a passed remove vote already removed the song; a passed skip vote needs a skip).
bool votesCast(const Session& s, VoteKind kind, uint32_t itemId, const String& nowUri,
               const String& nowAdderToken, String& message, bool& passed);

VoteInfo votesFor(VoteKind kind, uint32_t itemId, const String& nowUri, uint32_t sessionId);

void votesTick(const String& nowUri);  // expire old votes; drop skip votes for older songs
