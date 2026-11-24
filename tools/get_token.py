"""One-time Spotify login for MoochBox.

Run from the project root:

    python3 tools/get_token.py

What it does:
  1. Reads SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET from .env
  2. Opens Spotify's login page in your browser
  3. Catches the redirect on http://127.0.0.1:8888/callback
  4. Swaps the code for tokens
  5. Saves SPOTIFY_REFRESH_TOKEN into .env (it is never printed)

The refresh token lasts 6 months. Run this again after that, or if
MoochBox's owner panel says "re-run get_token.py".

Standard library only.
"""

import base64
import json
import secrets
import sys
import threading
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer

from envfile import ENV_PATH, load_env, set_env_value

HOST = "127.0.0.1"
PORT = 8888
REDIRECT_URI = f"http://{HOST}:{PORT}/callback"
SCOPES = " ".join([
    "user-modify-playback-state",   # add to queue, skip, pause/play
    "user-read-playback-state",     # devices, playback state
    "user-read-currently-playing",  # now playing
    "user-read-private",            # search (to be safe)
])
AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
TIMEOUT_S = 300


def fail(message: str) -> None:
    print(f"\n✗ {message}")
    sys.exit(1)


def wait_for_code(expected_state: str) -> str:
    """Run a tiny local server until Spotify redirects back with a code."""
    result = {}
    done = threading.Event()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            url = urllib.parse.urlparse(self.path)
            if url.path != "/callback":
                self.send_response(404)
                self.end_headers()
                return
            query = urllib.parse.parse_qs(url.query)
            if query.get("state", [""])[0] != expected_state:
                result["error"] = "state mismatch (try again)"
            elif "error" in query:
                result["error"] = f"Spotify said: {query['error'][0]}"
            elif "code" in query:
                result["code"] = query["code"][0]
            else:
                result["error"] = "no code in the redirect"

            ok = "code" in result
            body = (
                "<h2>MoochBox: Spotify connected ✓</h2><p>You can close this tab.</p>"
                if ok else
                f"<h2>MoochBox: login failed</h2><p>{result['error']}</p>"
            )
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(body.encode("utf-8"))
            done.set()

        def log_message(self, *args):  # keep the terminal quiet
            pass

    try:
        server = HTTPServer((HOST, PORT), Handler)
    except OSError:
        fail(f"Port {PORT} is busy. Close whatever is using it and try again.")

    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    finished = done.wait(TIMEOUT_S)
    server.shutdown()

    if not finished:
        fail(f"No login after {TIMEOUT_S // 60} minutes. Run the script again.")
    if "error" in result:
        fail(result["error"])
    return result["code"]


def exchange_code(code: str, client_id: str, client_secret: str) -> dict:
    basic = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    data = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI,
    }).encode()
    request = urllib.request.Request(
        TOKEN_URL,
        data=data,
        headers={
            "Authorization": f"Basic {basic}",
            "Content-Type": "application/x-www-form-urlencoded",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.loads(response.read())
    except urllib.error.HTTPError as err:
        detail = err.read().decode(errors="replace")
        if "invalid_client" in detail:
            fail("Spotify rejected the Client ID / Secret. Check them in .env.")
        fail(f"Token request failed ({err.code}): {detail}")
    except urllib.error.URLError as err:
        if "CERTIFICATE_VERIFY_FAILED" in str(err):
            fail("Python can't verify HTTPS certificates. If you installed Python "
                 "from python.org, run 'Install Certificates.command' in "
                 "/Applications/Python 3.x, then try again.")
        fail(f"Couldn't reach Spotify: {err.reason}")


def main() -> None:
    if not ENV_PATH.exists():
        fail(f"No .env found at {ENV_PATH}. Copy .env.example to .env first.")

    env = load_env()
    client_id = env.get("SPOTIFY_CLIENT_ID", "")
    client_secret = env.get("SPOTIFY_CLIENT_SECRET", "")
    if not client_id or not client_secret:
        fail("Fill in SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET in .env first.")

    state = secrets.token_urlsafe(16)
    login_url = AUTH_URL + "?" + urllib.parse.urlencode({
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPES,
        "state": state,
        "show_dialog": "true",
    })

    print("MoochBox · Spotify login")
    print(f"Make sure your Spotify app's Redirect URI is exactly: {REDIRECT_URI}")
    print("Opening Spotify in your browser. Log in with the Premium (owner) account...")
    if not webbrowser.open(login_url):
        print("\nCouldn't open a browser. Open this link yourself:\n" + login_url)

    code = wait_for_code(state)
    tokens = exchange_code(code, client_id, client_secret)

    refresh_token = tokens.get("refresh_token")
    if not refresh_token:
        fail("Spotify didn't return a refresh token. Run the script again.")

    granted = set(tokens.get("scope", "").split())
    missing = set(SCOPES.split()) - granted
    set_env_value("SPOTIFY_REFRESH_TOKEN", refresh_token)

    print("\n✓ Saved SPOTIFY_REFRESH_TOKEN to .env (valid for 6 months).")
    if missing:
        print(f"! Spotify didn't grant: {', '.join(sorted(missing))}")
    print("Next: python3 tools/make_secrets.py (coming next)")


if __name__ == "__main__":
    main()
