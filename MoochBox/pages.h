// Phone page, embedded as a string (no filesystem upload needed).
// Join · owner login · now playing · up next · search/add/swap/remove · owner card.
#pragma once

static const char PAGE_HTML[] = R"MOOCH(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>MoochBox</title>
<style>
  :root { --bg:#0e0f12; --card:#181a20; --line:#2a2d36; --text:#f2f3f5; --muted:#9aa0ab;
          --green:#1ed760; --red:#ff5a5f; --amber:#ffb020; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text);
         font:16px/1.4 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
         display:flex; justify-content:center; padding:20px 14px 40px; }
  main { width:100%; max-width:420px; }
  h1 { font-size:26px; margin:4px 0 2px; text-align:center; }
  h2 { font-size:13px; letter-spacing:.08em; text-transform:uppercase; color:var(--muted); margin:0 0 10px; }
  .sub { color:var(--muted); text-align:center; margin:0 0 18px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:16px; padding:16px; margin-bottom:14px; }
  label { display:block; font-size:14px; color:var(--muted); margin:0 0 6px; }
  input { width:100%; padding:13px; margin:0 0 14px; border-radius:12px; border:1px solid var(--line);
          background:#0b0c0f; color:var(--text); font-size:17px; }
  input.code { letter-spacing:12px; text-align:center; font-size:28px; }
  button { width:100%; padding:13px; border:0; border-radius:12px; background:var(--green); color:#000;
           font-size:16px; font-weight:700; }
  button.ghost { background:transparent; color:var(--muted); font-weight:500; margin-top:6px; }
  button.danger { background:transparent; color:var(--red); border:1px solid var(--red); }
  button.danger.armed { background:var(--red); color:#000; }
  button.small { width:auto; padding:7px 12px; font-size:14px; border-radius:10px; }
  button.outline { background:transparent; color:var(--text); border:1px solid var(--line); font-weight:500; }
  button:disabled { opacity:.45; }
  .error { color:var(--red); min-height:20px; margin:-4px 0 10px; font-size:14px; }
  .hint { background:#1d2a20; border:1px solid #2c5a3a; color:#cfeedd; border-radius:12px;
          padding:12px 14px; font-size:14px; margin-bottom:14px; }
  .warn { background:#2c2413; border-color:#6b5520; color:#ffe3a8; }
  .badge { display:inline-block; padding:3px 10px; border-radius:99px; background:#24262e; font-size:13px; }
  .bigcode { font-size:48px; font-weight:800; letter-spacing:14px; text-align:center; margin:6px 0; }
  .row { display:flex; justify-content:space-between; align-items:center; gap:8px; color:var(--muted); font-size:14px; margin:4px 0; }
  .song { display:flex; align-items:center; gap:12px; padding:10px 0; border-top:1px solid var(--line); }
  .song:first-of-type { border-top:0; }
  .tile { width:44px; height:44px; border-radius:10px; flex:none; display:flex; align-items:center;
          justify-content:center; font-weight:800; color:#000; font-size:15px; }
  .info { flex:1; min-width:0; }
  .title { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; font-weight:600; }
  .meta { color:var(--muted); font-size:13px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .actions { display:flex; gap:6px; flex:none; }
  .bar { height:4px; background:var(--line); border-radius:4px; margin-top:10px; overflow:hidden; }
  .bar > div { height:100%; background:var(--green); width:0; }
  .lock { color:var(--amber); font-size:13px; }
  .empty { color:var(--muted); font-size:14px; padding:6px 0; }
  .toast { position:fixed; left:50%; bottom:22px; transform:translateX(-50%); background:#24262e;
           border:1px solid var(--line); padding:10px 16px; border-radius:12px; font-size:14px;
           max-width:90%; opacity:0; transition:opacity .2s; pointer-events:none; }
  .toast.show { opacity:1; }
  select { background:#0b0c0f; color:var(--text); border:1px solid var(--line); border-radius:10px; padding:7px 10px; font-size:14px; }
  .controls { display:flex; gap:8px; margin-top:12px; }
  .controls button { flex:1; }
  .person { padding:10px 0; border-top:1px solid var(--line); }
  .person .btns { display:flex; flex-wrap:wrap; gap:6px; margin-top:8px; }
  .voted { border-color:var(--green) !important; color:var(--green) !important; }
  .divider { height:1px; background:var(--line); margin:14px 0; }
  .hidden { display:none !important; }
</style>
</head>
<body>
<main>
  <h1>🎵 MoochBox</h1>
  <p class="sub" id="subtitle">Join the jam</p>

  <!-- iPhone pop-up browser: nudge to Safari -->
  <div class="hint hidden" id="safariHint"></div>

  <!-- Join -->
  <section id="join" class="card hidden">
    <label for="name">Your name</label>
    <input id="name" maxlength="20" autocomplete="nickname" placeholder="Priya">
    <label for="code">Room code</label>
    <input id="code" class="code" inputmode="numeric" pattern="[0-9]*" maxlength="4" placeholder="····">
    <div class="error" id="joinError"></div>
    <button id="joinBtn">Join the jam</button>
    <button class="ghost" id="showOwner">Owner login</button>
  </section>

  <!-- Owner login -->
  <section id="ownerLogin" class="card hidden">
    <label for="password">Owner password</label>
    <input id="password" type="password" autocomplete="current-password">
    <div class="error" id="ownerError"></div>
    <button id="ownerBtn">Log in</button>
    <button class="ghost" id="showJoin">Back</button>
  </section>

  <!-- In the room -->
  <section id="room" class="hidden">
    <div class="hint warn hidden" id="statusHint"></div>

    <div class="card">
      <h2>Now playing</h2>
      <div id="nowBox"></div>
      <div class="controls hidden" id="hostControls">
        <button class="small outline" id="skipBtn">⏭ Skip</button>
        <button class="small outline" id="playBtn">⏸ Pause</button>
      </div>
      <div class="controls hidden" id="skipVoteRow">
        <button class="small outline" id="voteSkipBtn">⏭ Vote skip</button>
      </div>
    </div>

    <div class="card">
      <div class="row" style="margin:0 0 10px"><h2 style="margin:0">Up next</h2><span id="mySlots"></span></div>
      <div id="queueBox"></div>
    </div>

    <div class="card">
      <h2 id="searchTitle">Add a song</h2>
      <div class="hint warn hidden" id="lockedHint">Adding songs is locked right now.</div>
      <div class="hint hidden" id="swapHint"></div>
      <input id="q" placeholder="Search a song or artist" enterkeyhint="search" autocomplete="off">
      <div id="resultsBox"></div>
    </div>

    <div class="card hidden" id="panelCard">
      <h2 id="panelTitle">Host panel</h2>

      <div id="ownerOnly" class="hidden">
        <label>Room code (only you can see this)</label>
        <div class="bigcode" id="roomCode">····</div>
        <div class="row"><span>Wrong code tries</span><span id="failedJoins">0</span></div>
        <div class="row"><span>Failed owner logins</span><span id="failedOwner">0</span></div>
        <div style="height:10px"></div>
        <button class="danger" id="newCodeBtn">New code (logs out all guests)</button>
        <div class="divider"></div>
      </div>

      <div class="row"><span>Adding songs</span>
        <button class="small outline" id="lockBtn">🔓 Open</button></div>
      <div class="row"><span>Songs per person</span>
        <select id="rLimit"><option value="0">no limit</option><option>1</option><option>2</option><option>3</option>
        <option>4</option><option>5</option><option>6</option><option>8</option><option>10</option></select></div>
      <div class="row"><span>Lock + send next song at</span>
        <select id="rLockAt"><option value="8">8 s</option><option value="10">10 s</option><option value="15">15 s</option>
        <option value="20">20 s</option><option value="30">30 s</option></select></div>
      <div class="row"><span>Vote to remove</span>
        <select id="rVoteRemove"><option value="1">on</option><option value="0">off</option></select></div>
      <div class="row"><span>Vote to skip</span>
        <select id="rVoteSkip"><option value="1">on</option><option value="0">off</option></select></div>
      <div class="row"><span>Votes needed</span>
        <span><select id="rVotePct"><option value="25">25%</option><option value="34">⅓</option><option value="50">50%</option>
        <option value="67">⅔</option><option value="100">all</option></select>
        min <select id="rVoteMin"><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option></select></span></div>

      <div class="divider"></div>
      <div class="row"><span>People in the room</span><span id="guestCount">0 / 10</span></div>
      <div id="peopleBox"></div>
    </div>

    <div class="row"><span id="meLine"></span><button class="ghost small" id="logoutBtn">Log out</button></div>
  </section>
</main>
<div class="toast" id="toast"></div>

<script>
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const fmt = (ms) => { const s = Math.max(0, Math.floor(ms / 1000)); return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0"); };
const hue = (s) => { let h = 0; for (const c of String(s)) h = (h * 31 + c.charCodeAt(0)) % 360; return h; };
const initials = (s) => String(s).split(/\s+/).filter(Boolean).slice(0, 2).map((w) => w[0].toUpperCase()).join("") || "♪";
const tile = (t) => `<div class="tile" style="background:hsl(${hue(t.title)},70%,62%)">${esc(initials(t.title))}</div>`;

const ua = navigator.userAgent;
const inPopup = /iPhone|iPad|iPod/.test(ua) && !/Safari\//.test(ua);  // iOS captive pop-up browser
let token = localStorage.getItem("moochToken");
let me = null, state = null, swapId = null, pollTimer = null, panelTimer = null, lastResults = [], panel = null;

function toast(msg) {
  const t = $("toast"); t.textContent = msg; t.classList.add("show");
  clearTimeout(toast.timer); toast.timer = setTimeout(() => t.classList.remove("show"), 2600);
}

function show(view) {
  ["join", "ownerLogin", "room"].forEach((v) => $(v).classList.toggle("hidden", v !== view));
  $("subtitle").textContent = view === "room" ? "You're in the room" : view === "ownerLogin" ? "Owner" : "Join the jam";
  updateSafariHint(view === "room");
}

function updateSafariHint(joined) {
  const h = $("safariHint");
  if (!inPopup) return h.classList.add("hidden");
  h.innerHTML = joined
    ? "You're in! For the best experience tap <b>Done</b>, then open <b>Safari</b> and go to <b>192.168.4.1</b>. You'll stay logged in."
    : "Tip: after joining, open <b>Safari</b> and go to <b>192.168.4.1</b>. This pop-up can close by itself.";
  h.classList.remove("hidden");
}

async function api(path, { method = "GET", body } = {}) {
  const headers = {};
  if (token) headers["Authorization"] = "Bearer " + token;
  if (body) headers["Content-Type"] = "application/x-www-form-urlencoded";
  const res = await fetch(path, { method, headers, body: body ? new URLSearchParams(body) : undefined });
  let data = {};
  try { data = await res.json(); } catch (e) {}
  if (res.status === 401 && token && path !== "/api/logout") { forget(); }
  return { ok: res.ok, status: res.status, data };
}

function forget() {
  token = null; me = null; localStorage.removeItem("moochToken");
  clearInterval(pollTimer); clearInterval(panelTimer);
  show("join");
}

function enterRoom(data) {
  token = data.token || token;
  localStorage.setItem("moochToken", token);
  me = { name: data.name, role: data.role };
  const badges = { owner: "👑 Owner", host: "⭐ Host", dj: "🎧 DJ", guest: "Guest" };
  $("meLine").textContent = badges[me.role] + " · " + me.name;
  const isHost = me.role === "owner" || me.role === "host";
  $("panelCard").classList.toggle("hidden", !isHost);
  $("ownerOnly").classList.toggle("hidden", me.role !== "owner");
  $("panelTitle").textContent = me.role === "owner" ? "Owner panel" : "Host panel";
  show("room");
  clearInterval(pollTimer); clearInterval(panelTimer);
  loadState(); pollTimer = setInterval(loadState, 3000);
  if (isHost) { loadPanel(); panelTimer = setInterval(loadPanel, 5000); }
}

// ---------- room state ----------

async function loadState() {
  let r;
  try { r = await api("/api/state"); } catch (e) { return; }
  if (!r.ok) return;
  state = r.data; state.at = Date.now();
  renderState();
}

function renderState() {
  if (!state) return;
  const s = state, hint = $("statusHint");
  const msgs = { no_device: "Waiting for the owner to open Spotify and press play.",
                 offline: "MoochBox lost its internet. Songs wait here until it's back.",
                 auth: "MoochBox's Spotify login broke. The owner needs to fix it.",
                 starting: "MoochBox is starting up..." };
  hint.textContent = msgs[s.status] || "";
  hint.classList.toggle("hidden", !msgs[s.status]);

  const n = s.now;
  if (n.active) {
    const prog = Math.min(n.durationMs, n.progressMs + (n.isPlaying ? Date.now() - s.at : 0));
    $("nowBox").innerHTML = `<div class="song">${tile(n)}<div class="info">
      <div class="title">${n.isPlaying ? "▶" : "⏸"} ${esc(n.title)}</div>
      <div class="meta">${esc(n.artist)}${n.addedBy ? " · added by " + esc(n.addedBy) : ""}</div></div>
      <div class="meta">${fmt(prog)} / ${fmt(n.durationMs)}</div></div>
      <div class="bar"><div style="width:${(100 * prog / Math.max(1, n.durationMs)).toFixed(1)}%"></div></div>`;
  } else {
    $("nowBox").innerHTML = `<div class="empty">Nothing playing.</div>`;
  }

  const canVoteSkip = n.active && n.skipVote && n.skipVote.on && !s.canControl && !n.skipVote.mine;
  $("hostControls").classList.toggle("hidden", !s.canControl);
  $("playBtn").textContent = n.active && n.isPlaying ? "⏸ Pause" : "▶ Play";
  $("skipVoteRow").classList.toggle("hidden", !canVoteSkip);
  if (canVoteSkip) {
    const v = n.skipVote, b = $("voteSkipBtn");
    b.textContent = v.count ? `⏭ Vote skip ${v.count}/${v.needed}` : "⏭ Vote skip";
    b.classList.toggle("voted", v.iVoted); b.disabled = v.iVoted;
  }
  $("lockedHint").classList.toggle("hidden", !(s.addingLocked && !s.canControl));

  $("mySlots").textContent = s.me.limit > 0 ? `your songs: ${s.me.waiting} / ${s.me.limit}` : "";
  const lockIn = s.msUntilLock >= 0 ? s.msUntilLock - (Date.now() - s.at) : -1;
  if (!s.queue.length) {
    $("queueBox").innerHTML = `<div class="empty">Nothing queued yet. Add a song below!</div>`;
  } else {
    $("queueBox").innerHTML = s.queue.map((it, i) => {
      let status = "";
      if (it.locked) status = `<div class="lock">🔒 Up next on Spotify</div>`;
      else if (i === 0 && lockIn >= 0 && lockIn <= 30000) status = `<div class="lock">⏳ locks in ${fmt(lockIn)}</div>`;
      const canEdit = !it.locked && (it.mine || s.canEditAll);
      let actions = "";
      if (canEdit) {
        actions = `<div class="actions">${it.mine ? `<button class="small outline" onclick="startSwap(${it.id})">Swap</button>` : ""}
           <button class="small outline" onclick="removeSong(${it.id})">Remove</button></div>`;
      } else if (!it.locked && !it.mine && s.voteRemoveOn) {
        const label = it.votes ? `Vote remove ${it.votes}/${it.needed}` : "Vote remove";
        actions = `<div class="actions"><button class="small outline${it.iVoted ? " voted" : ""}" ${it.iVoted ? "disabled" : ""}
           onclick="voteRemove(${it.id})">${label}</button></div>`;
      }
      return `<div class="song">${tile(it)}<div class="info"><div class="title">${esc(it.title)}</div>
        <div class="meta">${esc(it.artist)} · ${it.mine ? "you" : esc(it.addedBy)}</div>${status}</div>${actions}</div>`;
    }).join("");
  }
}
setInterval(renderState, 1000);  // keep the progress bar and countdown moving

// ---------- search / add / swap / remove ----------

async function search() {
  const q = $("q").value.trim();
  if (!q) return;
  $("q").blur();
  $("resultsBox").innerHTML = `<div class="empty">Searching...</div>`;
  const start = await api("/api/search", { method: "POST", body: { q } });
  if (!start.ok) { $("resultsBox").innerHTML = `<div class="error">${esc(start.data.error || "Search failed.")}</div>`; return; }
  for (let i = 0; i < 40; i++) {
    await new Promise((r) => setTimeout(r, 400));
    const r = await api("/api/search?job=" + start.data.job);
    if (!r.ok) break;
    if (r.data.status === "pending") continue;
    if (r.data.status === "error") { $("resultsBox").innerHTML = `<div class="error">${esc(r.data.error)}</div>`; return; }
    lastResults = r.data.results;
    return renderResults();
  }
  $("resultsBox").innerHTML = `<div class="error">Search timed out. Try again.</div>`;
}

function renderResults() {
  if (!lastResults.length) { $("resultsBox").innerHTML = `<div class="empty">No results.</div>`; return; }
  const label = swapId ? "Swap in" : "Add";
  $("resultsBox").innerHTML = lastResults.map((t, i) => `<div class="song">${tile(t)}<div class="info">
    <div class="title">${esc(t.title)}</div><div class="meta">${esc(t.artist)} · ${fmt(t.durationMs)}</div></div>
    <button class="small" onclick="pick(${i}, this)">${label}</button></div>`).join("");
}

async function pick(i, btn) {
  const t = lastResults[i];
  btn.disabled = true;
  const r = swapId
    ? await api("/api/queue/swap", { method: "POST", body: { id: swapId, uri: t.uri } })
    : await api("/api/queue/add", { method: "POST", body: { uri: t.uri } });
  btn.disabled = false;
  if (!r.ok) return toast(r.data.error || "Couldn't do that.");
  toast(swapId ? "Swapped ✓" : "Added ✓");
  cancelSwap();
  $("q").value = ""; lastResults = []; $("resultsBox").innerHTML = "";
  loadState();
}

function startSwap(id) {
  swapId = id;
  const it = state.queue.find((x) => x.id === id);
  $("swapHint").innerHTML = `Swapping <b>${esc(it ? it.title : "song")}</b>: search for the new one. <a href="#" onclick="cancelSwap();return false" style="color:inherit">Cancel</a>`;
  $("swapHint").classList.remove("hidden");
  $("searchTitle").textContent = "Swap a song";
  $("q").focus();
  renderResults();
}

function cancelSwap() {
  swapId = null;
  $("swapHint").classList.add("hidden");
  $("searchTitle").textContent = "Add a song";
  if (lastResults.length) renderResults();
}

async function removeSong(id) {
  const r = await api("/api/queue/remove", { method: "POST", body: { id } });
  if (!r.ok) return toast(r.data.error || "Couldn't remove it.");
  toast("Removed");
  loadState();
}

// ---------- votes + host controls ----------

async function voteRemove(id) {
  const r = await api("/api/vote", { method: "POST", body: { kind: "remove", id } });
  if (!r.ok) return toast(r.data.error || "Couldn't vote.");
  toast(r.data.passed ? "Vote passed: song removed" : "Voted");
  loadState();
}

$("voteSkipBtn").onclick = async () => {
  const r = await api("/api/vote", { method: "POST", body: { kind: "skip" } });
  if (!r.ok) return toast(r.data.error || "Couldn't vote.");
  toast(r.data.passed ? "Vote passed: skipping" : "Voted to skip");
  loadState();
};

async function hostAction(path, body, okMsg) {
  const r = await api(path, { method: "POST", body });
  if (!r.ok) { toast(r.data.error || "Couldn't do that."); return false; }
  if (okMsg) toast(okMsg);
  setTimeout(loadState, 1200);
  loadPanel();
  return true;
}

$("skipBtn").onclick = () => hostAction("/api/host/skip", null, "Skipping...");
$("playBtn").onclick = () => hostAction("/api/host/playpause", null, "");
$("lockBtn").onclick = () => hostAction("/api/host/lock", { on: panel && panel.rules.addingLocked ? "0" : "1" }, "");

const ruleFields = { rLimit: "limit", rLockAt: "lockAt", rVoteRemove: "voteRemove", rVoteSkip: "voteSkip", rVotePct: "votePct", rVoteMin: "voteMin" };
for (const [id, key] of Object.entries(ruleFields)) {
  $(id).onchange = () => hostAction("/api/host/rules", { [key]: $(id).value }, "Saved");
}

// ---------- panel (owner + hosts) ----------

function setSelect(id, value) {
  const el = $(id), v = String(value);
  if (![...el.options].some((o) => (o.value || o.text) === v)) el.add(new Option(v, v));
  el.value = v;
}

async function loadPanel() {
  let r;
  try { r = await api("/api/panel"); } catch (e) { return; }
  if (!r.ok) return;
  panel = r.data;
  if (me.role === "owner") {
    $("roomCode").textContent = panel.code;
    $("failedJoins").textContent = panel.failedJoins;
    $("failedOwner").textContent = panel.failedOwner;
  }
  const ru = panel.rules;
  $("lockBtn").textContent = ru.addingLocked ? "🔒 Locked" : "🔓 Open";
  if (document.activeElement.tagName !== "SELECT") {
    setSelect("rLimit", ru.limit); setSelect("rLockAt", ru.lockAt);
    setSelect("rVoteRemove", ru.voteRemove ? 1 : 0); setSelect("rVoteSkip", ru.voteSkip ? 1 : 0);
    setSelect("rVotePct", ru.votePct); setSelect("rVoteMin", ru.voteMin);
  }
  $("guestCount").textContent = panel.guests + " / 10";
  renderPeople();
}

function renderPeople() {
  const badges = { host: "⭐ ", dj: "🎧 ", guest: "" };
  const isOwner = me.role === "owner";
  if (!panel.people.length) { $("peopleBox").innerHTML = `<div class="empty">Nobody has joined yet.</div>`; return; }
  $("peopleBox").innerHTML = panel.people.map((p) => {
    let btns = "";
    if (!p.me) {
      if (isOwner) btns += p.role === "host"
        ? `<button class="small outline" onclick="setRole(${p.id},'guest')">Remove host</button>`
        : `<button class="small outline" onclick="setRole(${p.id},'host')">⭐ Make host</button>`;
      if (p.role !== "host") btns += p.role === "dj"
        ? `<button class="small outline" onclick="setRole(${p.id},'guest')">Remove DJ</button>`
        : `<button class="small outline" onclick="setRole(${p.id},'dj')">🎧 Make DJ</button>`;
      if (isOwner || p.role !== "host") btns += `<button class="small danger" onclick="kick(${p.id}, this)">Kick</button>`;
    }
    return `<div class="person"><div class="row" style="color:var(--text);margin:0">
      <span>${badges[p.role] || ""}${esc(p.name)}${p.me ? " (you)" : ""}</span>
      <span class="meta">${p.songs} song${p.songs === 1 ? "" : "s"}</span></div>
      ${btns ? `<div class="btns">${btns}</div>` : ""}</div>`;
  }).join("");
}

function setRole(id, role) { hostAction("/api/host/role", { id, role }, "Role updated"); }

// Two taps instead of confirm(): the iPhone pop-up browser blocks dialogs.
function twoTap(btn, armedText, onConfirm) {
  if (!btn.classList.contains("armed")) {
    const original = btn.textContent;
    btn.classList.add("armed"); btn.textContent = armedText;
    btn._t = setTimeout(() => { btn.classList.remove("armed"); btn.textContent = original; }, 4000);
    return;
  }
  clearTimeout(btn._t);
  btn.classList.remove("armed");
  onConfirm();
}

function kick(id, btn) {
  twoTap(btn, "Tap again", () => hostAction("/api/host/kick", { id },
    me.role === "owner" ? "Kicked. New room code made." : "Kicked. The owner has the new room code."));
}

$("newCodeBtn").onclick = () => twoTap($("newCodeBtn"), "Tap again to confirm", async () => {
  $("newCodeBtn").textContent = "New code (logs out all guests)";
  const r = await api("/api/owner/new-code", { method: "POST" });
  if (!r.ok) return toast(r.data.error || "Couldn't make a new code.");
  toast("New code: " + r.data.code);
  loadPanel();
});

// ---------- join / login ----------

async function submit(path, body, errorId, button) {
  $(errorId).textContent = "";
  button.disabled = true;
  try {
    const r = await api(path, { method: "POST", body });
    if (!r.ok) { $(errorId).textContent = r.data.error || "Something went wrong."; return; }
    enterRoom(r.data);
  } catch (e) {
    $(errorId).textContent = "Can't reach MoochBox. Are you on its WiFi?";
  } finally {
    button.disabled = false;
  }
}

$("joinBtn").onclick = () => submit("/api/join", { name: $("name").value, code: $("code").value }, "joinError", $("joinBtn"));
$("ownerBtn").onclick = () => submit("/api/owner/login", { password: $("password").value }, "ownerError", $("ownerBtn"));
$("showOwner").onclick = () => show("ownerLogin");
$("showJoin").onclick = () => show("join");
$("logoutBtn").onclick = async () => { await api("/api/logout", { method: "POST" }).catch(() => {}); forget(); };
$("code").addEventListener("input", (e) => { e.target.value = e.target.value.replace(/\D/g, "").slice(0, 4); });
$("code").addEventListener("keydown", (e) => { if (e.key === "Enter") $("joinBtn").click(); });
$("password").addEventListener("keydown", (e) => { if (e.key === "Enter") $("ownerBtn").click(); });
$("q").addEventListener("keydown", (e) => { if (e.key === "Enter") search(); });

// On load: saved token → this phone's session (e.g. coming from the pop-up) → join form.
(async () => {
  try {
    if (token) {
      const r = await api("/api/me");
      if (r.ok) return enterRoom(r.data);
    }
    const r = await api("/api/resume");
    if (r.ok) return enterRoom(r.data);
  } catch (e) {}
  show("join");
})();
</script>
</body>
</html>
)MOOCH";
