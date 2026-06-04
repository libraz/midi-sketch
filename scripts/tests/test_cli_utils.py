"""Tests for cli_utils: run_cli and the shared ProgressCounter."""

import sys
import unittest
from pathlib import Path

# conftest puts scripts/ on the path; import the shared utilities.
import conftest  # noqa: F401  (ensures sys.path setup)
from cli_utils import ProgressCounter, run_cli


class _FakeResult:
    """Minimal stand-in mimicking the various TestResult dataclasses."""

    def __init__(self, **attrs):
        for key, value in attrs.items():
            setattr(self, key, value)


class TestRunCli(unittest.TestCase):
    """Test the run_cli subprocess wrapper."""

    def test_success_returns_zero(self):
        rc, msg = run_cli(sys.executable, ["-c", "pass"], Path.cwd(), timeout=30)
        self.assertEqual(rc, 0)
        self.assertEqual(msg, "")

    def test_nonzero_exit_captures_stderr(self):
        rc, msg = run_cli(
            sys.executable,
            ["-c", "import sys; sys.stderr.write('boom'); sys.exit(3)"],
            Path.cwd(),
            timeout=30,
        )
        self.assertEqual(rc, 3)
        self.assertIn("boom", msg)

    def test_stderr_snippet_truncated_to_200(self):
        rc, msg = run_cli(
            sys.executable,
            ["-c", "import sys; sys.stderr.write('x' * 500); sys.exit(1)"],
            Path.cwd(),
            timeout=30,
        )
        self.assertEqual(rc, 1)
        self.assertEqual(len(msg), 200)

    def test_timeout_returns_none(self):
        rc, msg = run_cli(
            sys.executable,
            ["-c", "import time; time.sleep(5)"],
            Path.cwd(),
            timeout=1,
        )
        self.assertIsNone(rc)
        self.assertIn("Timeout", msg)

    def test_missing_binary_returns_none(self):
        rc, msg = run_cli(
            "/nonexistent/path/to/cli_binary_xyz", [], Path.cwd(), timeout=5
        )
        self.assertIsNone(rc)
        self.assertTrue(msg)

    def test_cwd_is_used(self):
        target = str(Path.cwd())
        rc, _ = run_cli(
            sys.executable,
            ["-c", f"import os,sys; sys.exit(0 if os.getcwd()==r'{target}' else 1)"],
            Path.cwd(),
            timeout=30,
        )
        self.assertEqual(rc, 0)


class TestProgressCounter(unittest.TestCase):
    """Test the shared thread-safe ProgressCounter."""

    def test_default_classify_error(self):
        counter = ProgressCounter(1)
        counter.increment(_FakeResult(error="boom"))
        self.assertEqual(counter.errors, 1)
        self.assertEqual(counter.current, 1)

    def test_default_classify_critical(self):
        counter = ProgressCounter(1)
        counter.increment(_FakeResult(error=None, has_critical=True))
        self.assertEqual(counter.failed, 1)

    def test_default_classify_high_severity(self):
        counter = ProgressCounter(1)
        counter.increment(_FakeResult(error=None, has_high_severity=True))
        self.assertEqual(counter.failed, 1)

    def test_default_classify_error_count(self):
        counter = ProgressCounter(1)
        counter.increment(_FakeResult(error=None, error_count=2))
        self.assertEqual(counter.failed, 1)

    def test_default_classify_warnings(self):
        counter = ProgressCounter(1)
        counter.increment(_FakeResult(error=None, has_warnings=True))
        self.assertEqual(counter.warned, 1)

    def test_default_classify_violations(self):
        counter = ProgressCounter(1)
        counter.increment(_FakeResult(error=None, has_violations=True))
        self.assertEqual(counter.warned, 1)

    def test_default_classify_passed(self):
        counter = ProgressCounter(1)
        counter.increment(_FakeResult(error=None))
        self.assertEqual(counter.passed, 1)

    def test_critical_takes_priority_over_warnings(self):
        counter = ProgressCounter(1)
        counter.increment(
            _FakeResult(error=None, has_critical=True, has_warnings=True)
        )
        self.assertEqual(counter.failed, 1)
        self.assertEqual(counter.warned, 0)

    def test_custom_classifier(self):
        counter = ProgressCounter(2, classifier=lambda r: "failed")
        counter.increment(_FakeResult())
        counter.increment(_FakeResult())
        self.assertEqual(counter.failed, 2)
        self.assertEqual(counter.passed, 0)

    def test_running_count(self):
        counter = ProgressCounter(3)
        for _ in range(3):
            counter.increment(_FakeResult(error=None))
        self.assertEqual(counter.current, 3)
        self.assertEqual(counter.passed, 3)


if __name__ == "__main__":
    unittest.main()
