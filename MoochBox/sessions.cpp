#include <Arduino.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include "sessions.h"
#include "secrets.h"

static const int MAX_SESSIONS = 12;            // 10 guests + owner + spare
static const int MAX_GUESTS = 10;
static const uint32_t SESSION_MS = 6UL * 60 * 60 * 1000;  // 6 hours

static const int JOIN_TRIES = 5;               // per phone
static const uint32_t JOIN_LOCK_MS = 60UL * 1000;
static const int GLOBAL_TRIES = 20;            // from everyone, per minute
static const uint32_t GLOBAL_WINDOW_MS = 60UL * 1000;
static const int OWNER_TRIES = 5;
static const uint32_t OWNER_LOCK_MS = 5UL * 60 * 1000;

static Session sessions[MAX_SESSIONS];
static uint32_t nextSessionId = 1;
static String code;

struct Attempts {
  IPAddress ip;
  uint8_t fails = 0;
  uint32_t lockedUntil = 0;
};
static Attempts joinAttempts[16];
static Attempts ownerAttempts[8];

static uint32_t globalWindowStart = 0;
static int globalFails = 0;
static uint32_t pausedUntil = 0;
static uint32_t totalJoinFails = 0;
static uint32_t totalOwnerFails = 0;

static bool timeLeft(uint32_t until) { return (int32_t)(until - millis()) > 0; }

const char* roleName(Role role) {
  switch (role) {
    case ROLE_OWNER: return "owner";
    case ROLE_HOST:  return "host";
    case ROLE_DJ:    return "dj";
    default:         return "guest";
  }
}

// ---------- room code ----------

static bool isEasyCode(const char* c) {
  bool same = c[0] == c[1] && c[1] == c[2] && c[2] == c[3];
  bool up = c[1] == c[0] + 1 && c[2] == c[1] + 1 && c[3] == c[2] + 1;
  bool down = c[1] == c[0] - 1 && c[2] == c[1] - 1 && c[3] == c[2] - 1;
  return same || up || down;
}

static void makeCode() {
  char buf[5];
  do {
    snprintf(buf, sizeof(buf), "%04lu", (unsigned long)(esp_random() % 10000));
  } while (isEasyCode(buf));
  code = buf;
}

void sessionsBegin() {
  makeCode();
  Serial.printf("Room code (owner only): %s\n", code.c_str());
}

const String& roomCode() { return code; }

void rotateRoomCode() {
  makeCode();
  Serial.printf("New room code (owner only): %s. Everyone stays in.\n", code.c_str());
}

void newRoomCode() {
  makeCode();
  for (Session& s : sessions) {
    if (s.used && (s.role == ROLE_GUEST || s.role == ROLE_DJ)) s = Session();
  }
  Serial.printf("New room code (owner only): %s. Guests and DJs logged out.\n", code.c_str());
}

// ---------- helpers ----------

static String newToken() {
  char buf[33];
  for (int i = 0; i < 4; i++) snprintf(buf + i * 8, 9, "%08lx", (unsigned long)esp_random());
  return String(buf);
}

static void dropExpired() {
  for (Session& s : sessions) {
    if (s.used && !timeLeft(s.expiresAt)) s = Session();
  }
}

static Session* freeSlot() {
  dropExpired();
  for (Session& s : sessions) {
    if (!s.used) return &s;
  }
  return nullptr;
}

static Attempts& attemptsFor(Attempts* list, int size, IPAddress ip) {
  Attempts* oldest = &list[0];
  for (int i = 0; i < size; i++) {
    if (list[i].ip == ip) return list[i];
    if (!timeLeft(list[i].lockedUntil) && list[i].fails <= oldest->fails) oldest = &list[i];
  }
  *oldest = Attempts();
  oldest->ip = ip;
  return *oldest;
}

static bool nameTaken(const String& name) {
  for (const Session& s : sessions) {
    if (s.used && s.name.equalsIgnoreCase(name)) return true;
  }
  return false;
}

// Trims, checks and de-duplicates a guest name. Empty result = not allowed.
static String cleanName(String name, String& message) {
  name.trim();
  if (name.isEmpty() || name.length() > 20) {
    message = "Pick a name of 1-20 characters.";
    return "";
  }
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (c == '<' || c == '>' || c == '&' || c == '"' || c == '\\' || (uint8_t)c < 32) {
      message = "Names can't contain < > & \" or \\.";
      return "";
    }
  }
  const char* blocked[] = {"host", "owner", "admin", "dj", "moochbox"};
  for (const char* b : blocked) {
    if (name.equalsIgnoreCase(b)) {
      message = "That name is reserved. Pick another.";
      return "";
    }
  }
  if (!nameTaken(name)) return name;
  for (int n = 2; n < 100; n++) {
    String candidate = name + " " + String(n);
    if (!nameTaken(candidate)) return candidate;
  }
  message = "Name taken.";
  return "";
}

static Session* startSession(const String& name, Role role, IPAddress ip) {
  Session* s = freeSlot();
  if (!s) return nullptr;
  s->used = true;
  s->id = nextSessionId++;
  s->token = newToken();
  s->name = name;
  s->role = role;
  s->expiresAt = millis() + SESSION_MS;
  s->ip = ip;
  return s;
}

// ---------- join ----------

AuthResult sessionsJoin(const String& rawName, const String& givenCode, IPAddress ip,
                        Session*& out, String& message) {
  out = nullptr;
  if (timeLeft(pausedUntil)) {
    message = "Too many wrong codes. Joining is paused for a minute.";
    return AUTH_PAUSED;
  }
  Attempts& a = attemptsFor(joinAttempts, 16, ip);
  if (timeLeft(a.lockedUntil)) {
    message = "Too many wrong codes. Try again in a minute.";
    return AUTH_LOCKED;
  }

  String trimmedCode = givenCode;
  trimmedCode.trim();
  if (trimmedCode != code) {
    totalJoinFails++;
    a.fails++;
    if (millis() - globalWindowStart > GLOBAL_WINDOW_MS) {
      globalWindowStart = millis();
      globalFails = 0;
    }
    if (++globalFails > GLOBAL_TRIES) pausedUntil = millis() + GLOBAL_WINDOW_MS;
    if (a.fails >= JOIN_TRIES) {
      a.fails = 0;
      a.lockedUntil = millis() + JOIN_LOCK_MS;
      message = "Wrong code. Too many tries: wait a minute.";
      return AUTH_LOCKED;
    }
    message = "Wrong code (" + String(JOIN_TRIES - a.fails) + " tries left).";
    return AUTH_WRONG;
  }
  a.fails = 0;

  String name = cleanName(rawName, message);
  if (name.isEmpty()) return AUTH_BAD_NAME;
  if (sessionsGuestCount() >= MAX_GUESTS || !(out = startSession(name, ROLE_GUEST, ip))) {
    message = "The room is full.";
    return AUTH_FULL;
  }
  return AUTH_OK;
}

// ---------- owner login ----------

static bool checkOwnerPassword(const String& password) {
  uint8_t salt[16];
  for (int i = 0; i < 16; i++) {
    char hex[3] = {OWNER_PASSWORD_SALT_HEX[i * 2], OWNER_PASSWORD_SALT_HEX[i * 2 + 1], 0};
    salt[i] = (uint8_t)strtoul(hex, nullptr, 16);
  }

  uint8_t digest[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, salt, sizeof(salt));
  mbedtls_sha256_update(&ctx, (const uint8_t*)password.c_str(), password.length());
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);

  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);

  // Compare every character so timing doesn't leak how much matched.
  uint8_t diff = 0;
  for (int i = 0; i < 64; i++) diff |= hex[i] ^ OWNER_PASSWORD_HASH_HEX[i];
  return diff == 0;
}

AuthResult sessionsOwnerLogin(const String& password, IPAddress ip, Session*& out, String& message) {
  out = nullptr;
  Attempts& a = attemptsFor(ownerAttempts, 8, ip);
  if (timeLeft(a.lockedUntil)) {
    message = "Too many wrong passwords. Try again in 5 minutes.";
    return AUTH_LOCKED;
  }
  if (!checkOwnerPassword(password)) {
    totalOwnerFails++;
    if (++a.fails >= OWNER_TRIES) {
      a.fails = 0;
      a.lockedUntil = millis() + OWNER_LOCK_MS;
      message = "Wrong password. Locked for 5 minutes.";
      return AUTH_LOCKED;
    }
    message = "Wrong password (" + String(OWNER_TRIES - a.fails) + " tries left).";
    return AUTH_WRONG;
  }
  a.fails = 0;

  out = startSession("Owner", ROLE_OWNER, ip);
  if (!out) {
    message = "No free session slot.";
    return AUTH_FULL;
  }
  return AUTH_OK;
}

// ---------- lookups ----------

Session* sessionsFind(const String& token) {
  if (token.length() != 32) return nullptr;
  for (Session& s : sessions) {
    if (s.used && s.token == token) {
      if (!timeLeft(s.expiresAt)) {
        s = Session();
        return nullptr;
      }
      return &s;
    }
  }
  return nullptr;
}

void sessionsLogout(const String& token) {
  Session* s = sessionsFind(token);
  if (s) *s = Session();
}

bool sessionsIpHasSession(IPAddress ip) {
  for (const Session& s : sessions) {
    if (s.used && s.ip == ip && timeLeft(s.expiresAt)) return true;
  }
  return false;
}

Session* sessionsFindByIp(IPAddress ip) {
  Session* best = nullptr;
  for (Session& s : sessions) {
    if (s.used && s.ip == ip && timeLeft(s.expiresAt) && (!best || (int32_t)(s.expiresAt - best->expiresAt) > 0)) {
      best = &s;
    }
  }
  return best;
}

int sessionsListPeople(Session* out, int max) {
  int n = 0;
  for (const Session& s : sessions) {
    if (n < max && s.used && s.role != ROLE_OWNER && timeLeft(s.expiresAt)) out[n++] = s;
  }
  return n;
}

Session* sessionsFindById(uint32_t id) {
  for (Session& s : sessions) {
    if (s.used && s.id == id && timeLeft(s.expiresAt)) return &s;
  }
  return nullptr;
}

int sessionsRemoveByIp(IPAddress ip, String* tokens, int maxTokens) {
  int n = 0;
  for (Session& s : sessions) {
    if (!s.used || s.ip != ip) continue;
    if (n < maxTokens) tokens[n] = s.token;
    n++;
    s = Session();
  }
  return n;
}

int sessionsActiveCount() {
  int n = 0;
  for (const Session& s : sessions) {
    if (s.used && timeLeft(s.expiresAt)) n++;
  }
  return n;
}

int sessionsGuestCount() {
  int n = 0;
  for (const Session& s : sessions) {
    if (s.used && s.role != ROLE_OWNER && timeLeft(s.expiresAt)) n++;
  }
  return n;
}

uint32_t sessionsFailedJoinTries() { return totalJoinFails; }
uint32_t sessionsFailedOwnerTries() { return totalOwnerFails; }
