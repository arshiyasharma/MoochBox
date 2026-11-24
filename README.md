# 🎵 MoochBox

**A shared Spotify queue on a $5 ESP32. One person has Premium; everyone else mooches.**

## The problem

Only Spotify Premium users can join a Spotify Jam, and most friends don't have Premium.

## The fix

MoochBox is logged in to Spotify **as the owner** (the one Premium account). Guests join MoochBox's WiFi, enter a 4-digit room code, and add songs from their phone's browser. MoochBox keeps the real queue and sends the next song to the owner's Spotify ~15 s before the current one ends.

- Guests need **no Spotify account, no Premium, no app**.
- Music plays on the **owner's phone**, like normal.
- **No server, no cloud, no cost**: the ESP32 is the server. The Spotify Web API is free.

```
 Guests' phones ──WiFi "MoochBox"──▶ ESP32 ──owner's hotspot──▶ Spotify API ──▶ owner's phone plays it
 (browser only)                     (queue,                                       (Premium)
                                     room code,
                                     roles, votes)
```

Full design: [plan.md](plan.md).

---

## What you need

| | |
|---|---|
| **Board** | Classic ESP32 dev board (ESP32-WROOM-32 / D0WD-V3) + USB cable |
| **LEDs** | 1 green + 1 red LED, 2 resistors (220 Ω–1 kΩ), jumper wires |
| **Spotify** | One **Premium** account (the owner) |
| **Phone** | Owner's phone with a 2.4 GHz hotspot (iPhone: *Maximize Compatibility* on) |
| **Laptop** | Arduino IDE 2, Python 3 |

---

## Setup (one time, ~20 min)

### 1. Wire the LEDs

| LED | Wiring |
|---|---|
| Green | GPIO 18 → resistor → long leg (+); short leg (−) → GND |
| Red | GPIO 19 → resistor → long leg (+); short leg (−) → GND |

### 2. Arduino IDE

1. **Boards Manager** → install **esp32 by Espressif** (3.x).
2. **Library Manager** → install:
   - **ArduinoJson** by Benoit Blanchon
   - **Async TCP** by ESP32Async
   - **ESP Async WebServer** by ESP32Async

   Remove any older `AsyncTCP` / `ESPAsyncWebServer` copies from `~/Documents/Arduino/libraries`: they clash.
3. **Tools** → Board **ESP32 Dev Module**, Partition Scheme **Huge APP (3MB No OTA)**, and your USB port.

### 3. Create the Spotify app

1. [developer.spotify.com](https://developer.spotify.com) → **Dashboard** → **Create app**.
2. Redirect URI: `http://127.0.0.1:8888/callback` (exactly).
3. Tick **Web API** → Save. Open **Settings** and copy the **Client ID** and **Client Secret**.

### 4. Fill in `.env`

```bash
cp .env.example .env
```

Open `.env` and fill in everything except the refresh token. Quote values with spaces.

| Key | What |
|---|---|
| `SPOTIFY_CLIENT_ID` / `SPOTIFY_CLIENT_SECRET` | From step 3 |
| `SPOTIFY_REFRESH_TOKEN` | Leave empty: step 5 fills it |
| `MOOCHBOX_OWNER_PASSWORD` | Owner login, 10+ characters, not reused anywhere |
| `MOOCHBOX_WIFI_PASSWORD` | Password for the `MoochBox` WiFi, 8+ characters |
| `MOOCHBOX_HOTSPOT_SSID` / `MOOCHBOX_HOTSPOT_PASSWORD` | Owner's phone hotspot, spelled exactly (e.g. `"Arshiyas iPhone"`) |

`.env` is gitignored. Never commit it.

### 5. Log in to Spotify once

```bash
python3 tools/get_token.py
```

Log in with the **Premium** account and click **Agree**. The script saves `SPOTIFY_REFRESH_TOKEN` into `.env` (it lasts 6 months; run this again after that).

### 6. Build `secrets.h`

```bash
python3 tools/make_secrets.py
```

Checks `.env` and writes the gitignored `MoochBox/secrets.h`. The owner password goes in only as a **salted SHA-256 hash**. Rerun this after **every** `.env` change.

### 7. Upload

Open `MoochBox/MoochBox.ino` in Arduino IDE → **Upload**. Open **Serial Monitor** at **115200** baud, line ending **Newline**.

---

## Running a party

### Owner

1. Turn on the phone hotspot (keep its settings screen open while MoochBox connects).
2. Plug in MoochBox. **Blinking green** = connecting, **solid green** = ready.
3. Open Spotify on the phone and **press play** (Spotify needs an active device).
4. Join the **MoochBox** WiFi → the page pops up → **Owner login** → owner password.
5. Read the **room code** off the owner panel. Tell friends the WiFi password + room code.

### Guests

1. Join the **MoochBox** WiFi with the WiFi password. The join page pops up (or open `http://192.168.4.1`).
2. Type a name + the 4-digit room code → **Join the jam**.
3. On iPhone, the pop-up is a mini-browser: tap **Done**, open **Safari**, go to `192.168.4.1`. You stay logged in.
4. Search, add, swap, remove, vote.

Guests get **no internet** through MoochBox: they can only reach MoochBox, so nobody browses on the owner's data.

---

## Roles

| Role | How you get it | Can do |
|---|---|---|
| 👑 **Owner** | Owner password (set only in `.env`) | Everything a host can, plus: make/remove hosts, see the room code, **New code**, kick hosts |
| ⭐ **Host** | Owner taps **Make host** | Skip, pause/play, lock adding, change rules, remove anyone's waiting song, make/remove DJs, kick guests and DJs. Can't see the room code or touch hosts. |
| 🎧 **DJ** | Owner or host taps **Make DJ** | Remove anyone's waiting song. Otherwise a guest. |
| 🙂 **Guest** | Name + room code | Search and add (up to the limit), swap/remove **own** waiting songs, vote |

- Names are just labels: power comes from the role stored on the ESP32. Names like `host`, `owner`, `admin`, `dj`, `moochbox` are blocked, and duplicates get a number (`Sam 2`).
- Logins last **6 hours**. The page remembers you, so refreshing keeps you in.

---

## How the queue works

| Rule | Default | Change it |
|---|---|---|
| Songs waiting per person | **3**. A slot frees when one of your songs **starts playing**. | Panel: off, 1–10 |
| Gap between your adds | 5 s | Fixed |
| Lock + send next song | **15 s** before the current song ends | Panel: 8–30 s |
| Queue order | First come, first served | — |
| Lock adding | Off | Panel toggle |

- **Why MoochBox keeps its own queue:** Spotify's API can add to a queue but can't remove or reorder. So the editable queue lives on MoochBox, and Spotify only ever gets the **one next song**.
- **Countdown:** 30 s before the lock, the top song shows `⏳ locks in 0:12`, the last chance to swap or vote it out.
- **Locked (🔒):** the song has been sent to Spotify and plays next. Nobody can change it.
- **Swap:** replace your song with another; it keeps its place in line.
- **Skipping safely:** if a host skips before the next song is locked, MoochBox sends the top song first, so Spotify doesn't autoplay a random one.
- Search hides repeats (single vs album versions) and shows up to 8 results. Songs show a colored tile with initials.

## Votes

| | Vote to remove | Vote to skip |
|---|---|---|
| On | Someone else's waiting song | The playing song (not one you added) |
| Passes | **50%** of everyone in the room, **at least 3** votes, within 60 s | Same |
| Then | Song removed, its adder gets the slot back | Spotify skips right away |

One vote per person. A new song resets the skip vote. Percent, minimum and on/off are in the panel.

## Room code and kicking

| | |
|---|---|
| **Room code** | 4 random digits, new **every boot** (easy ones like `1234` skipped). Only the owner sees it. Never stored. |
| **Wrong codes** | 5 tries per phone → 60 s lockout. 20+ wrong tries a minute from anyone → joining pauses 60 s. The owner panel counts them. |
| **Owner password** | 5 wrong tries per phone → 5-minute lockout. |
| **Kick** (owner/host) | That person is out (every login from their phone), their waiting songs go, and the **room code changes**. Everyone else stays in. |
| **New code** (owner) | Fresh code + logs out **every** guest and DJ. Owner and hosts stay. |

## Owner / host panel

Under **Now playing**: ⏭ Skip, ⏸ Pause / ▶ Play. Further down: lock adding, the rules above, the people list (make host / make DJ / kick, two taps to confirm), and a guide to the roles. The owner also sees the room code, failed-try counters and **New code**.

## Status lights

| Green | Red | Meaning |
|---|---|---|
| Blinking | — | Connecting |
| Solid | — | All good |
| Double flash | — | A song was sent to Spotify |
| — | Solid | No internet, or Spotify login broken |
| — | Blinking | No active Spotify device: owner, press play |

## Serial Monitor commands (testing)

`<song name>` search · `1`–`5` add straight to Spotify · `now` what's playing · `code` room code · `scan` nearby WiFi names · `help`

---

## Project layout

```
.env / .env.example       secrets (gitignored) / template
plan.md                   full design and decisions
tools/
  get_token.py            one-time Spotify login → refresh token in .env
  make_secrets.py         .env → MoochBox/secrets.h (owner password hashed)
  envfile.py              shared .env reader/writer
MoochBox/                 Arduino sketch (open MoochBox.ino)
  MoochBox.ino            setup, loop, Serial commands
  net.*                   hotspot connection + MoochBox WiFi + captive portal DNS
  spotify.*               Spotify Web API: token, search, queue, now playing, skip/pause/play
  engine.*                all Spotify work in loop(): search jobs, polling, lock + send, skip
  moochqueue.*            the queue: limits, swap/remove, permissions
  sessions.*              room code, logins, roles, lockouts, kicks
  votes.*                 vote to remove / vote to skip
  web.*                   HTTP API for the phone page
  pages.h                 the phone page (HTML/CSS/JS)
  leds.*                  green/red status LEDs
  secrets.h               generated, gitignored
```

## Troubleshooting

| Problem | Fix |
|---|---|
| Can't find the hotspot | `scan` in Serial Monitor shows exact names. Copy yours into `.env`, rerun `make_secrets.py`, upload. Turn on *Maximize Compatibility*. |
| `secrets.h is missing` when compiling | `python3 tools/make_secrets.py` |
| "Owner needs to press play" / red blinking | Open Spotify on the owner's phone and play anything |
| "Spotify login failed" / red solid | Rerun `get_token.py`, then `make_secrets.py`, then upload |
| Sketch too big | Partition Scheme → **Huge APP (3MB No OTA)** |
| Join page doesn't pop up | Open `http://192.168.4.1` in the browser |
| Button does nothing in the iPhone pop-up | Use Safari at `192.168.4.1` |

## Limits

- ~10 phones at once (ESP32 WiFi limit).
- The local page is plain HTTP. With the WiFi password, WPA2 encrypts it over the air: party-level security, not bank-level.
- The queue lives in RAM: restarting MoochBox clears it (and makes a new room code).
- Spotify's development mode: personal, non-commercial use only.
