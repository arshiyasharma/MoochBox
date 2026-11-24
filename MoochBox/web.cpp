#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "web.h"
#include "net.h"
#include "sessions.h"
#include "moochqueue.h"
#include "engine.h"
#include "votes.h"
#include "pages.h"

static AsyncWebServer server(80);

// ---------- helpers ----------

static void sendJson(AsyncWebServerRequest* req, int status, JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  req->send(status, "application/json", body);
}

static void sendError(AsyncWebServerRequest* req, int status, const String& message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(req, status, doc);
}

static void sendOk(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(req, 200, doc);
}

static String param(AsyncWebServerRequest* req, const char* name) {
  if (req->hasParam(name, true)) return req->getParam(name, true)->value();  // form body
  if (req->hasParam(name)) return req->getParam(name)->value();              // query string
  return String();
}

static IPAddress clientIp(AsyncWebServerRequest* req) { return req->client()->remoteIP(); }

// "Authorization: Bearer <token>" → the session, or nullptr. Caller holds STATE_LOCK.
static Session* currentSession(AsyncWebServerRequest* req) {
  if (!req->hasHeader("Authorization")) return nullptr;
  String value = req->header("Authorization");
  if (!value.startsWith("Bearer ")) return nullptr;
  return sessionsFind(value.substring(7));
}

static int authStatus(AuthResult r) {
  switch (r) {
    case AUTH_OK:       return 200;
    case AUTH_WRONG:    return 401;
    case AUTH_BAD_NAME: return 400;
    case AUTH_FULL:     return 503;
    default:            return 429;  // locked / paused
  }
}

static void sendSession(AsyncWebServerRequest* req, const Session* s) {
  JsonDocument doc;
  doc["token"] = s->token;
  doc["name"] = s->name;
  doc["role"] = roleName(s->role);
  sendJson(req, 200, doc);
}

static void redirectToPortal(AsyncWebServerRequest* req) {
  req->redirect("http://" + MOOCHBOX_IP.toString() + "/");
}

static void addTrackJson(JsonObject o, const Track& t) {
  o["uri"] = t.uri;
  o["title"] = t.title;
  o["artist"] = t.artist;
  o["durationMs"] = t.durationMs;
}

// Runs `body` with the session (and STATE_LOCK held), or answers 401/403.
template <typename F>
static void withSession(AsyncWebServerRequest* req, Role minRole, F body) {
  STATE_LOCK;
  Session* s = currentSession(req);
  if (!s) return sendError(req, 401, "Not logged in.");
  if (s->role < minRole) return sendError(req, 403, "Not allowed.");
  body(*s);
}

// ---------- captive portal ----------
// Phones probe these URLs to check for internet. Before joining we redirect them
// to our page (so the join page pops up). After joining we answer "success",
// so the phone treats MoochBox as signed in and stays connected.
static void captiveCheck(AsyncWebServerRequest* req) {
  bool joined;
  {
    STATE_LOCK;
    joined = sessionsIpHasSession(clientIp(req));
  }
  if (!joined) return redirectToPortal(req);

  String url = req->url();
  if (url == "/generate_204" || url == "/gen_204") {
    req->send(204);
  } else if (url == "/connecttest.txt") {
    req->send(200, "text/plain", "Microsoft Connect Test");
  } else if (url == "/ncsi.txt") {
    req->send(200, "text/plain", "Microsoft NCSI");
  } else {  // Apple: /hotspot-detect.html, /library/test/success.html
    req->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  }
}

// ---------- routes ----------

static void accountRoutes() {
  server.on("/api/join", HTTP_POST, [](AsyncWebServerRequest* req) {
    STATE_LOCK;
    Session* s = nullptr;
    String message;
    AuthResult r = sessionsJoin(param(req, "name"), param(req, "code"), clientIp(req), s, message);
    if (r != AUTH_OK) return sendError(req, authStatus(r), message);
    Serial.printf("Joined: %s\n", s->name.c_str());
    sendSession(req, s);
  });

  server.on("/api/owner/login", HTTP_POST, [](AsyncWebServerRequest* req) {
    STATE_LOCK;
    Session* s = nullptr;
    String message;
    AuthResult r = sessionsOwnerLogin(param(req, "password"), clientIp(req), s, message);
    if (r != AUTH_OK) {
      Serial.printf("Owner login failed from %s\n", clientIp(req).toString().c_str());
      return sendError(req, authStatus(r), message);
    }
    Serial.println("Owner logged in.");
    sendSession(req, s);
  });

  // Moving from the iPhone pop-up to Safari: pick up this phone's session.
  server.on("/api/resume", HTTP_GET, [](AsyncWebServerRequest* req) {
    STATE_LOCK;
    Session* s = sessionsFindByIp(clientIp(req));
    if (!s) return sendError(req, 401, "Not logged in.");
    sendSession(req, s);
  });

  server.on("/api/me", HTTP_GET, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_GUEST, [&](Session& s) { sendSession(req, &s); });
  });

  server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* req) {
    STATE_LOCK;
    Session* s = currentSession(req);
    if (s) sessionsLogout(s->token);
    req->send(204);
  });
}

static void queueRoutes() {
  // Everything the guest page shows, polled every few seconds.
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_GUEST, [&](Session& s) {
      JsonDocument doc;
      EngineView v = engineView();
      const QueueRules& rules = queueRules();

      doc["status"] = boxStatusName(v.status);
      JsonObject me = doc["me"].to<JsonObject>();
      me["name"] = s.name;
      me["role"] = roleName(s.role);
      me["waiting"] = queueCountFor(s.token);
      me["limit"] = rules.perPersonLimit;

      JsonObject now = doc["now"].to<JsonObject>();
      now["active"] = v.now.active;
      if (v.now.active) {
        addTrackJson(now, v.now.track);
        now["isPlaying"] = v.now.isPlaying;
        now["progressMs"] = v.now.progressMs + (v.now.isPlaying ? v.polledAgoMs : 0);
        now["addedBy"] = v.nowAddedBy;
      }

      if (v.now.active) {
        const QueueRules& r = queueRules();
        VoteInfo sv = votesFor(VOTE_SKIP, 0, v.now.track.uri, s.id);
        JsonObject skip = now["skipVote"].to<JsonObject>();
        skip["on"] = r.voteSkipOn;
        skip["count"] = sv.count;
        skip["needed"] = sv.needed;
        skip["iVoted"] = sv.iVoted;
        skip["mine"] = v.nowAdderToken.length() && v.nowAdderToken == s.token;
      }
      doc["msUntilLock"] = v.msUntilLock;
      doc["voteRemoveOn"] = rules.voteRemoveOn;
      doc["canControl"] = s.role >= ROLE_HOST;
      doc["addingLocked"] = rules.addingLocked;
      JsonArray queue = doc["queue"].to<JsonArray>();
      for (const QueueItem& it : queueItems()) {
        JsonObject o = queue.add<JsonObject>();
        o["id"] = it.id;
        addTrackJson(o, it.track);
        o["addedBy"] = it.addedBy;
        o["mine"] = it.ownerToken == s.token;
        o["locked"] = it.state == ITEM_LOCKED;
        VoteInfo rv = votesFor(VOTE_REMOVE, it.id, "", s.id);
        o["votes"] = rv.count;
        o["needed"] = rv.needed;
        o["iVoted"] = rv.iVoted;
      }
      doc["canEditAll"] = s.role >= ROLE_DJ;
      sendJson(req, 200, doc);
    });
  });

  // Search is two steps so the web server never waits on Spotify:
  // POST starts a job, GET ?job= returns it once loop() has run it.
  server.on("/api/search", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_GUEST, [&](Session&) {
      String q = param(req, "q");
      q.trim();
      if (q.isEmpty() || q.length() > 80) return sendError(req, 400, "Type a song name.");
      int job = engineStartSearch(q);
      if (job < 0) return sendError(req, 429, "Busy, try again in a second.");
      JsonDocument doc;
      doc["job"] = job;
      sendJson(req, 202, doc);
    });
  });

  server.on("/api/search", HTTP_GET, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_GUEST, [&](Session&) {
      static Track results[SEARCH_RESULTS];  // under STATE_LOCK, so one at a time
      int count = 0;
      String error;
      JobStatus st = engineSearchResult(param(req, "job").toInt(), results, count, error);
      JsonDocument doc;
      if (st == JOB_PENDING) {
        doc["status"] = "pending";
      } else if (st == JOB_DONE) {
        doc["status"] = "done";
        JsonArray arr = doc["results"].to<JsonArray>();
        for (int i = 0; i < count; i++) addTrackJson(arr.add<JsonObject>(), results[i]);
      } else {
        doc["status"] = "error";
        doc["error"] = st == JOB_NONE ? "Search expired. Try again." : error;
      }
      sendJson(req, 200, doc);
    });
  });

  server.on("/api/queue/add", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_GUEST, [&](Session& s) {
      Track t;
      if (!engineCachedTrack(param(req, "uri"), t)) return sendError(req, 400, "Search for the song again.");
      String message;
      if (!queueAdd(s, t, message)) return sendError(req, 409, message);
      Serial.printf("+ %s added %s · %s\n", s.name.c_str(), t.title.c_str(), t.artist.c_str());
      sendOk(req);
    });
  });

  server.on("/api/queue/remove", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_GUEST, [&](Session& s) {
      String message;
      if (!queueRemove(s, param(req, "id").toInt(), message)) return sendError(req, 409, message);
      sendOk(req);
    });
  });

  server.on("/api/vote", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_GUEST, [&](Session& s) {
      String kind = param(req, "kind");
      EngineView v = engineView();
      String nowUri = v.now.active ? v.now.track.uri : String();
      bool passed = false;
      String message;
      VoteKind k = kind == "skip" ? VOTE_SKIP : VOTE_REMOVE;
      if (!votesCast(s, k, param(req, "id").toInt(), nowUri, v.nowAdderToken, message, passed)) {
        return sendError(req, 409, message);
      }
      if (passed && k == VOTE_SKIP) engineRequest(CMD_SKIP);
      if (passed) Serial.printf("Vote passed: %s\n", k == VOTE_SKIP ? "skip" : "remove");
      JsonDocument doc;
      doc["passed"] = passed;
      sendJson(req, 200, doc);
    });
  });

  server.on("/api/queue/swap", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_GUEST, [&](Session& s) {
      Track t;
      if (!engineCachedTrack(param(req, "uri"), t)) return sendError(req, 400, "Search for the song again.");
      String message;
      if (!queueSwap(s, param(req, "id").toInt(), t, message)) return sendError(req, 409, message);
      sendOk(req);
    });
  });
}

static Role parseRole(const String& r) {
  if (r == "host") return ROLE_HOST;
  if (r == "dj") return ROLE_DJ;
  return ROLE_GUEST;
}

static void panelRoutes() {
  // Owner/host panel. Hosts see everything except the room code and owner stats.
  server.on("/api/panel", HTTP_GET, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_HOST, [&](Session& s) {
      const QueueRules& r = queueRules();
      JsonDocument doc;
      if (s.role == ROLE_OWNER) {
        doc["code"] = roomCode();
        doc["failedJoins"] = sessionsFailedJoinTries();
        doc["failedOwner"] = sessionsFailedOwnerTries();
      }
      JsonObject rules = doc["rules"].to<JsonObject>();
      rules["limit"] = r.perPersonLimit;
      rules["lockAt"] = r.lockAtMs / 1000;
      rules["voteRemove"] = r.voteRemoveOn;
      rules["voteSkip"] = r.voteSkipOn;
      rules["votePct"] = r.votePct;
      rules["voteMin"] = r.voteMin;
      rules["addingLocked"] = r.addingLocked;

      static Session people[12];  // under STATE_LOCK
      int n = sessionsListPeople(people, 12);
      doc["guests"] = n;
      JsonArray arr = doc["people"].to<JsonArray>();
      for (int i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = people[i].id;
        o["name"] = people[i].name;
        o["role"] = roleName(people[i].role);
        o["songs"] = queueCountFor(people[i].token);
        o["me"] = people[i].id == s.id;
      }
      sendJson(req, 200, doc);
    });
  });

  server.on("/api/host/skip", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_HOST, [&](Session& s) {
      engineRequest(CMD_SKIP);
      Serial.printf("%s skipped\n", s.name.c_str());
      sendOk(req);
    });
  });

  server.on("/api/host/playpause", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_HOST, [&](Session&) {
      EngineView v = engineView();
      engineRequest(v.now.active && v.now.isPlaying ? CMD_PAUSE : CMD_PLAY);
      sendOk(req);
    });
  });

  server.on("/api/host/lock", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_HOST, [&](Session&) {
      queueRules().addingLocked = param(req, "on") == "1";
      sendOk(req);
    });
  });

  server.on("/api/host/rules", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_HOST, [&](Session&) {
      QueueRules& r = queueRules();
      if (req->hasParam("limit", true)) r.perPersonLimit = constrain(param(req, "limit").toInt(), 0, 10);
      if (req->hasParam("lockAt", true)) r.lockAtMs = constrain(param(req, "lockAt").toInt(), 8, 30) * 1000UL;
      if (req->hasParam("voteRemove", true)) r.voteRemoveOn = param(req, "voteRemove") == "1";
      if (req->hasParam("voteSkip", true)) r.voteSkipOn = param(req, "voteSkip") == "1";
      if (req->hasParam("votePct", true)) r.votePct = constrain(param(req, "votePct").toInt(), 10, 100);
      if (req->hasParam("voteMin", true)) r.voteMin = constrain(param(req, "voteMin").toInt(), 1, 10);
      sendOk(req);
    });
  });

  // Roles (plan §4.1): only the owner makes/removes hosts; hosts and the
  // owner make/remove DJs. Nobody can change the owner or themselves.
  server.on("/api/host/role", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_HOST, [&](Session& s) {
      Session* target = sessionsFindById(param(req, "id").toInt());
      if (!target) return sendError(req, 404, "That person left.");
      if (target->id == s.id) return sendError(req, 403, "You can't change your own role.");
      if (target->role == ROLE_OWNER) return sendError(req, 403, "Nobody can change the owner.");
      Role want = parseRole(param(req, "role"));
      bool touchesHost = want == ROLE_HOST || target->role == ROLE_HOST;
      if (touchesHost && s.role != ROLE_OWNER) return sendError(req, 403, "Only the owner can make or remove hosts.");
      target->role = want;
      Serial.printf("%s is now %s\n", target->name.c_str(), roleName(want));
      sendOk(req);
    });
  });

  // Kick (plan §4.6): that person (every session from their phone) is out,
  // their waiting songs go, and the room code changes so they can't rejoin.
  // Everyone else stays in. The owner shares the new code with newcomers.
  server.on("/api/host/kick", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_HOST, [&](Session& s) {
      Session* target = sessionsFindById(param(req, "id").toInt());
      if (!target) return sendError(req, 404, "That person left.");
      if (target->id == s.id) return sendError(req, 403, "You can't kick yourself.");
      if (target->role == ROLE_OWNER) return sendError(req, 403, "Nobody can kick the owner.");
      if (target->role == ROLE_HOST && s.role != ROLE_OWNER) return sendError(req, 403, "Only the owner can kick a host.");
      if (target->ip == s.ip) return sendError(req, 403, "That's your own phone.");

      String name = target->name;
      String tokens[6];
      int n = sessionsRemoveByIp(target->ip, tokens, 6);
      for (int i = 0; i < min(n, 6); i++) queueRemoveAllFor(tokens[i]);
      rotateRoomCode();
      Serial.printf("%s kicked %s\n", s.name.c_str(), name.c_str());
      sendOk(req);
    });
  });

  server.on("/api/owner/new-code", HTTP_POST, [](AsyncWebServerRequest* req) {
    withSession(req, ROLE_OWNER, [&](Session&) {
      newRoomCode();
      JsonDocument doc;
      doc["code"] = roomCode();
      sendJson(req, 200, doc);
    });
  });
}

void webBegin() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html; charset=utf-8", PAGE_HTML);
  });

  const char* probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
                          "/library/test/success.html", "/connecttest.txt",
                          "/ncsi.txt", "/success.txt", "/canonical.html"};
  for (const char* p : probes) server.on(p, HTTP_GET, captiveCheck);

  accountRoutes();
  queueRoutes();
  panelRoutes();

  // Anything else: other hosts (captive portal) go to our page; our own
  // unknown paths get a 404.
  server.onNotFound([](AsyncWebServerRequest* req) {
    if (req->host() != MOOCHBOX_IP.toString()) return redirectToPortal(req);
    req->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("✓ Web server running.");
}
