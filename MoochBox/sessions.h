// Room code, roles, sessions and lockouts (plan §4).
// Not thread-safe by itself: callers hold STATE_LOCK (moochqueue.h).
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

enum Role { ROLE_GUEST, ROLE_DJ, ROLE_HOST, ROLE_OWNER };

struct Session {
  bool used = false;
  uint32_t id = 0;         // public id for the panel (the token stays secret)
  String token;
  String name;
  Role role = ROLE_GUEST;
  uint32_t expiresAt = 0;  // millis()
  IPAddress ip;
  uint32_t lastAddAt = 0;  // millis() of this person's last added song (spam gap)
};

enum AuthResult {
  AUTH_OK,
  AUTH_WRONG,        // wrong room code / owner password
  AUTH_LOCKED,       // this phone tried too many times
  AUTH_PAUSED,       // too many wrong tries from everyone: joining paused
  AUTH_BAD_NAME,
  AUTH_FULL,
};

const char* roleName(Role role);

void sessionsBegin();                // makes the first room code
const String& roomCode();
void newRoomCode();                  // fresh code + logs out every guest and DJ
void rotateRoomCode();               // fresh code, nobody logged out (used by kick)

// On AUTH_OK, `out` points at the new session. Otherwise `message` says why.
AuthResult sessionsJoin(const String& name, const String& code, IPAddress ip,
                        Session*& out, String& message);
AuthResult sessionsOwnerLogin(const String& password, IPAddress ip,
                              Session*& out, String& message);

Session* sessionsFind(const String& token);  // nullptr if missing or expired
void sessionsLogout(const String& token);
bool sessionsIpHasSession(IPAddress ip);
// Newest valid session from this phone. Lets the page move from the iPhone
// pop-up browser to Safari without logging in again.
Session* sessionsFindByIp(IPAddress ip);
// Copies the guests/DJs/hosts (not the owner) into `out`; returns how many.
int sessionsListPeople(Session* out, int max);

Session* sessionsFindById(uint32_t id);
// Kick: ends every session from this phone; returns how many. `tokens` gets their tokens.
int sessionsRemoveByIp(IPAddress ip, String* tokens, int maxTokens);
int sessionsActiveCount();           // everyone in the room, owner included

int sessionsGuestCount();
uint32_t sessionsFailedJoinTries();
uint32_t sessionsFailedOwnerTries();
