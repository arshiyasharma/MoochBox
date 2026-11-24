# tools

Laptop-side Python scripts (Python 3, standard library only). Run them from the project root.

| Script | What it does | Status |
|---|---|---|
| `get_token.py` | One-time Spotify login; saves `SPOTIFY_REFRESH_TOKEN` into `.env` (never printed) | ✅ Ready |
| `envfile.py` | Shared helper: reads/updates `.env` | ✅ Ready |
| `make_secrets.py` | Checks `.env`, hashes the owner password, writes the gitignored `MoochBox/secrets.h`. Rerun after every `.env` change. | ✅ Ready |
