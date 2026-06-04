"""Tests for music_analyzer.issue_parser: Issue dataclass and parse_issues."""

import unittest

import conftest  # noqa: F401  (ensures sys.path setup)
from music_analyzer.issue_parser import Issue, parse_issues


class TestParseIssues(unittest.TestCase):
    """Test parsing analysis.json issue entries."""

    def test_empty(self):
        self.assertEqual(parse_issues({}), [])
        self.assertEqual(parse_issues({"issues": []}), [])

    def test_simultaneous_clash(self):
        analysis = {
            "issues": [
                {
                    "type": "simultaneous_clash",
                    "severity": "high",
                    "tick": 1920,
                    "bar": 2,
                    "beat": 1.0,
                    "interval_name": "minor 2nd",
                    "interval_semitones": 1,
                    "notes": [
                        {"track": "chord", "pitch": 60, "name": "C4",
                         "provenance": {"source": "chord_voicing"}},
                        {"track": "bass", "pitch": 61, "name": "C#4",
                         "provenance": {"source": "bass_pattern"}},
                    ],
                }
            ]
        }
        issues = parse_issues(analysis)
        self.assertEqual(len(issues), 1)
        issue = issues[0]
        self.assertEqual(issue.type, "simultaneous_clash")
        self.assertEqual(issue.interval_name, "minor 2nd")
        self.assertEqual(issue.interval_semitones, 1)
        # track_pair is sorted alphabetically
        self.assertEqual(issue.track_pair, ("bass", "chord"))
        self.assertEqual(len(issue.clash_notes), 2)
        self.assertIn("chord_voicing", issue.provenance_source)
        self.assertIn("bass_pattern", issue.provenance_source)

    def test_clash_with_single_note_has_empty_pair(self):
        analysis = {
            "issues": [
                {
                    "type": "simultaneous_clash",
                    "notes": [
                        {"track": "chord", "pitch": 60, "name": "C4",
                         "provenance": {"source": "chord_voicing"}},
                    ],
                }
            ]
        }
        issue = parse_issues(analysis)[0]
        self.assertEqual(issue.track_pair, ("", ""))

    def test_sustained_over_chord_change(self):
        analysis = {
            "issues": [
                {
                    "type": "sustained_over_chord_change",
                    "severity": "medium",
                    "tick": 480,
                    "bar": 1,
                    "beat": 2.0,
                    "track": "vocal",
                    "pitch": 64,
                    "pitch_name": "E4",
                    "original_chord": "C",
                    "new_chord": "G",
                    "new_chord_tones": ["G", "B", "D"],
                    "provenance": {"source": "melody_phrase", "original_pitch": 64},
                }
            ]
        }
        issue = parse_issues(analysis)[0]
        self.assertEqual(issue.type, "sustained_over_chord_change")
        self.assertEqual(issue.original_chord, "C")
        self.assertEqual(issue.new_chord, "G")
        self.assertEqual(issue.chord_tones, ["G", "B", "D"])
        self.assertEqual(issue.provenance_source, "melody_phrase")

    def test_sustained_uses_generation_source_fallback(self):
        analysis = {
            "issues": [
                {
                    "type": "sustained_over_chord_change",
                    "track": "vocal",
                    "provenance": {"generation_source": "hook"},
                }
            ]
        }
        issue = parse_issues(analysis)[0]
        self.assertEqual(issue.provenance_source, "hook")

    def test_non_diatonic_note(self):
        analysis = {
            "issues": [
                {
                    "type": "non_diatonic_note",
                    "severity": "low",
                    "track": "motif",
                    "pitch": 61,
                    "pitch_name": "C#4",
                    "key": "C major",
                    "provenance": {"source": "motif", "original_pitch": 61},
                }
            ]
        }
        issue = parse_issues(analysis)[0]
        self.assertEqual(issue.type, "non_diatonic_note")
        self.assertEqual(issue.provenance_source, "motif")
        self.assertIn("non-diatonic", issue.description)

    def test_generic_issue(self):
        analysis = {
            "issues": [
                {
                    "type": "non_chord_tone",
                    "severity": "medium",
                    "track": "chord",
                    "pitch": 65,
                    "pitch_name": "F4",
                    "chord_name": "C",
                    "chord_tones": ["C", "E", "G"],
                    "provenance": {"generation_source": "chord_voicing"},
                }
            ]
        }
        issue = parse_issues(analysis)[0]
        self.assertEqual(issue.type, "non_chord_tone")
        self.assertEqual(issue.chord_name, "C")
        self.assertEqual(issue.provenance_source, "chord_voicing")

    def test_mixed_issue_types(self):
        analysis = {
            "issues": [
                {"type": "non_chord_tone", "track": "chord",
                 "provenance": {"generation_source": "chord_voicing"}},
                {"type": "non_diatonic_note", "track": "motif",
                 "provenance": {"source": "motif"}},
            ]
        }
        issues = parse_issues(analysis)
        self.assertEqual(len(issues), 2)
        self.assertEqual(issues[0].type, "non_chord_tone")
        self.assertEqual(issues[1].type, "non_diatonic_note")


class TestIssueDataclass(unittest.TestCase):
    """Test the Issue dataclass defaults."""

    def test_defaults(self):
        issue = Issue(
            type="x", severity="low", tick=0, bar=1, beat=1.0,
            track="vocal", pitch=60, pitch_name="C4",
            chord_name="", chord_tones=[],
        )
        self.assertEqual(issue.provenance_source, "")
        self.assertEqual(issue.clash_notes, [])
        self.assertEqual(issue.track_pair, ("", ""))


if __name__ == "__main__":
    unittest.main()
