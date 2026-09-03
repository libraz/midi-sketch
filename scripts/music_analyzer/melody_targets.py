"""Resolution and loading of the melody discipline target profile.

The two-layer melody discipline evaluator judges a generated vocal against
measurements taken from a corpus of real songs. The corpus itself is licensed
material that is not distributed with the repository; the profile derived from
it is written next to the corpus by ``build_reference_targets.py`` and read from
there by default. ``MIDISKETCH_MELODY_TARGETS`` (or ``--melody-targets``)
overrides that location.

The profile being absent is never silent: the caller gets a reason it can put in
its report, because a melody-discipline layer that quietly disappears reads as a
layer that passed.
"""

import json
import os
from pathlib import Path
from typing import Optional, Tuple

#: Environment variable holding the path of the target profile.
TARGETS_ENV_VAR = "MIDISKETCH_MELODY_TARGETS"

#: Where ``build_reference_targets.py`` writes the profile by default.
DEFAULT_TARGETS_PATH = (
    Path(__file__).resolve().parents[2] / "backup" / "reference" / "target_profiles.json"
)

_override_path: Optional[Path] = None
_cache: dict = {}


def set_targets_path(path) -> None:
    """Point the evaluator at a target profile for the rest of this process.

    Passing None returns to the environment variable.
    """
    global _override_path
    _override_path = Path(path) if path else None
    _cache.clear()


def configured_path() -> Optional[Path]:
    """Explicitly configured target profile path, if any.

    Returns None when neither the setter nor the environment variable was used,
    i.e. when the default location applies.
    """
    if _override_path is not None:
        return _override_path
    value = os.environ.get(TARGETS_ENV_VAR)
    return Path(value) if value else None


def targets_path() -> Path:
    """Path the profile is read from: the configured one, else the default."""
    return configured_path() or DEFAULT_TARGETS_PATH


def load_targets() -> Tuple[Optional[dict], Optional[str]]:
    """Load the target profile.

    Returns:
        (targets, reason). Exactly one is None: on success the profile, on
        failure a reason phrased for the report so an inactive layer is visible
        instead of being mistaken for a clean result.
    """
    path = targets_path()
    key = str(path)
    if key in _cache:
        return _cache[key], None

    try:
        targets = json.loads(path.read_text())
    except OSError as exc:
        hint = "" if configured_path() else f"; set {TARGETS_ENV_VAR} to read it from elsewhere"
        return None, (f"the reference target profile at {path} could not be read "
                      f"({exc}){hint}")
    except json.JSONDecodeError as exc:
        return None, f"the reference target profile at {path} is not valid JSON ({exc})"

    if not isinstance(targets, dict):
        return None, f"the reference target profile at {path} is not an object"

    _cache[key] = targets
    return targets, None
