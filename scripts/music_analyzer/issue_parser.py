"""Shared dissonance Issue dataclass and analysis.json parser.

Consolidates the Issue dataclass and parse_issues() previously
duplicated between check_dissonance.py and check_rhythmlock.py. This
parser reads the ``issues`` array produced by the C++ CLI's --analyze
output (analysis.json) and normalizes each entry into an Issue object.

Note: this is distinct from music_analyzer.models.Issue, which models
issues produced by the Python MusicAnalyzer. This Issue mirrors the
fields emitted by the C++ dissonance analyzer.
"""

from dataclasses import dataclass, field
from typing import List


@dataclass
class Issue:
    """A single dissonance issue parsed from analysis.json."""

    type: str
    severity: str
    tick: int
    bar: int
    beat: float
    track: str
    pitch: int
    pitch_name: str
    chord_name: str
    chord_tones: List[str]
    provenance_source: str = ""
    original_pitch: int = 0
    description: str = ""
    # For simultaneous_clash: involved notes
    clash_notes: List[dict] = field(default_factory=list)
    interval_name: str = ""
    interval_semitones: int = 0
    # For sustained_over_chord_change
    original_chord: str = ""
    new_chord: str = ""
    # Track pair (for clash analysis)
    track_pair: tuple = ("", "")


def parse_issues(analysis: dict) -> List[Issue]:
    """Parse issues from analysis.json into Issue objects."""
    issues = []
    for item in analysis.get("issues", []):
        issue_type = item.get("type", "")

        # Handle provenance - may be in different locations based on issue type
        prov = item.get("provenance", {})
        prov_source = prov.get("generation_source", "") or prov.get("source", "")

        if issue_type == "simultaneous_clash":
            # Clash has multiple notes involved
            notes = item.get("notes", [])
            # Collect provenance sources from all notes
            sources = [n.get("provenance", {}).get("source", "") for n in notes]
            # Extract track pair
            tracks = sorted([n.get("track", "") for n in notes])
            track_pair = (tracks[0], tracks[1]) if len(tracks) >= 2 else ("", "")

            interval_semitones = item.get("interval_semitones", 0)

            issues.append(Issue(
                type=issue_type,
                severity=item.get("severity", ""),
                tick=item.get("tick", 0),
                bar=item.get("bar", 0),
                beat=item.get("beat", 0),
                track=", ".join(n.get("track", "") for n in notes),
                pitch=notes[0].get("pitch", 0) if notes else 0,
                pitch_name=", ".join(n.get("name", "") for n in notes),
                chord_name="",
                chord_tones=[],
                provenance_source=", ".join(set(s for s in sources if s)),
                original_pitch=0,
                description=f"{item.get('interval_name', '')} clash",
                clash_notes=notes,
                interval_name=item.get("interval_name", ""),
                interval_semitones=interval_semitones,
                track_pair=track_pair,
            ))
        elif issue_type == "sustained_over_chord_change":
            # Sustained note over chord change
            issues.append(Issue(
                type=issue_type,
                severity=item.get("severity", ""),
                tick=item.get("tick", 0),
                bar=item.get("bar", 0),
                beat=item.get("beat", 0),
                track=item.get("track", ""),
                pitch=item.get("pitch", 0),
                pitch_name=item.get("pitch_name", ""),
                chord_name=item.get("new_chord", ""),
                chord_tones=item.get("new_chord_tones", []),
                provenance_source=prov_source,
                original_pitch=prov.get("original_pitch", 0),
                description=f"held over {item.get('original_chord', '')} -> {item.get('new_chord', '')}",
                original_chord=item.get("original_chord", ""),
                new_chord=item.get("new_chord", ""),
            ))
        elif issue_type == "non_diatonic_note":
            # Non-diatonic note
            prov = item.get("provenance", {})
            issues.append(Issue(
                type=issue_type,
                severity=item.get("severity", ""),
                tick=item.get("tick", 0),
                bar=item.get("bar", 0),
                beat=item.get("beat", 0),
                track=item.get("track", ""),
                pitch=item.get("pitch", 0),
                pitch_name=item.get("pitch_name", ""),
                chord_name="",
                chord_tones=[],
                provenance_source=prov.get("source", ""),
                original_pitch=prov.get("original_pitch", 0),
                description=f"non-diatonic in {item.get('key', 'C major')}",
            ))
        else:
            # Regular issue (non_chord_tone, etc.)
            prov = item.get("provenance", {})
            issues.append(Issue(
                type=issue_type,
                severity=item.get("severity", ""),
                tick=item.get("tick", 0),
                bar=item.get("bar", 0),
                beat=item.get("beat", 0),
                track=item.get("track", ""),
                pitch=item.get("pitch", 0),
                pitch_name=item.get("pitch_name", ""),
                chord_name=item.get("chord_name", ""),
                chord_tones=item.get("chord_tones", []),
                provenance_source=prov.get("generation_source", ""),
                original_pitch=prov.get("original_pitch", 0),
                description="",
            ))
    return issues
