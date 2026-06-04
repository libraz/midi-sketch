"""Shared CLI invocation and progress utilities for check scripts.

Consolidates the thread-safe progress counter and the subprocess
invocation scaffold that were previously duplicated across
check_dissonance.py, check_rhythmlock.py, check_pitch_crossing.py,
and music_analyzer/runner.py.
"""

import subprocess
import threading
from pathlib import Path
from typing import Callable, Optional, Tuple


def run_cli(
    cli_path: str,
    args_list: list,
    work_dir: Path,
    timeout: int = 60,
) -> Tuple[Optional[int], str]:
    """Invoke the CLI and return (returncode, message).

    On success, returns (returncode, stderr_snippet). The caller is
    responsible for reading any output files the CLI produced in
    work_dir.

    On timeout the returncode is None and the message describes the
    timeout. On any other exception the returncode is None and the
    message contains the (truncated) exception text. The stderr snippet
    is always truncated to 200 characters to match prior behavior.

    Args:
        cli_path: Path to the CLI binary (relative paths resolved
            against the process cwd, not work_dir).
        args_list: Arguments to pass after the binary path.
        work_dir: Working directory for the subprocess.
        timeout: Timeout in seconds.

    Returns:
        (returncode, message): returncode is an int on completion or
        None on timeout/exception. message is a stderr snippet (success
        or non-zero exit) or an error description (timeout/exception).
    """
    cmd = [cli_path, *args_list]
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=work_dir,
        )
        return result.returncode, result.stderr[:200]
    except subprocess.TimeoutExpired:
        return None, f"Timeout (>{timeout}s)"
    except Exception as exc:  # noqa: BLE001 - intentionally broad
        return None, str(exc)[:200]


class ProgressCounter:
    """Thread-safe progress counter for batch testing.

    Tracks a running ``current`` count plus three classification
    buckets: ``failed``, ``warned`` and ``errors``. A fourth bucket,
    ``passed``, is also tracked for runners that want it. Classification
    is delegated to a callable so that each script can supply its own
    notion of failure/warning while sharing the locking machinery.

    The callable receives the result object and must return one of the
    strings ``"error"``, ``"failed"``, ``"warned"`` or ``"passed"``.

    If no classifier is supplied, a default one is used that inspects the
    result defensively via attributes commonly present on the various
    TestResult dataclasses:
      - ``result.error``                -> "error"
      - ``result.has_critical``         -> "failed"
      - ``result.has_high_severity``    -> "failed"
      - ``result.error_count > 0``      -> "failed"
      - ``result.has_warnings``         -> "warned"
      - ``result.has_violations``       -> "warned"
      - otherwise                       -> "passed"
    """

    def __init__(
        self,
        total: int,
        classifier: Optional[Callable[[object], str]] = None,
    ):
        self.total = total
        self.current = 0
        self.lock = threading.Lock()
        self.passed = 0
        self.failed = 0
        self.warned = 0
        self.errors = 0
        self._classify = classifier or self._default_classify

    @staticmethod
    def _default_classify(result: object) -> str:
        """Defensively classify a result by reading optional attrs."""
        if getattr(result, "error", None):
            return "error"
        if getattr(result, "has_critical", False):
            return "failed"
        if getattr(result, "has_high_severity", False):
            return "failed"
        if getattr(result, "error_count", 0):
            return "failed"
        if getattr(result, "has_warnings", False):
            return "warned"
        if getattr(result, "has_violations", False):
            return "warned"
        return "passed"

    def increment(self, result: object):
        """Increment the running count and classify the result."""
        with self.lock:
            self.current += 1
            bucket = self._classify(result)
            if bucket == "error":
                self.errors += 1
            elif bucket == "failed":
                self.failed += 1
            elif bucket == "warned":
                self.warned += 1
            else:
                self.passed += 1
