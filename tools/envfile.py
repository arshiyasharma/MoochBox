"""Read and update the project's .env file (standard library only).

Format: one KEY=value per line. Values may be wrapped in "double" or
'single' quotes. Lines starting with # are comments.
"""

from pathlib import Path

ENV_PATH = Path(__file__).resolve().parent.parent / ".env"


def _unquote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    return value


def load_env(path: Path = ENV_PATH) -> dict:
    """Return {KEY: value} from the .env file. Missing file -> {}."""
    env = {}
    if not path.exists():
        return env
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        env[key.strip()] = _unquote(value)
    return env


def set_env_value(key: str, value: str, path: Path = ENV_PATH) -> None:
    """Set KEY to value in the .env file, keeping every other line as-is."""
    if '"' in value or "\n" in value:
        raise ValueError(f"{key} value can't contain quotes or newlines")
    new_line = f'{key}="{value}"'
    lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped.startswith("#") and stripped.split("=", 1)[0].strip() == key:
            lines[i] = new_line
            break
    else:
        lines.append(new_line)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    path.chmod(0o600)
