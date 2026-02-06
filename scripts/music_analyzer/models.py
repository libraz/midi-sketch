"""Data classes for music analysis.

All data model classes used by the music analyzer: Note, Issue,
QualityScore, HookPattern, AnalysisResult, and TestResult.
"""

from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple

from .constants import TRACK_NAMES, Severity, Category


# Forward reference for tick_to_bar to avoid circular imports.
# We inline the computation here to keep models self-contained.
def _tick_to_bar(tick: int) -> int:
    """Convert tick to bar number (1-indexed). Internal helper."""
    return tick // 1920 + 1


@dataclass
class Note:
    """Represents a MIDI note."""
    start: int
    duration: int
    pitch: int
    velocity: int
    channel: int
    provenance: Optional[dict] = None

    @property
    def end(self) -> int:
        return self.start + self.duration

    @property
    def track_name(self) -> str:
        return TRACK_NAMES.get(self.channel, f"Ch{self.channel}")

    @property
    def bar(self) -> int:
        return _tick_to_bar(self.start)


@dataclass
class Bonus:
    """Represents a positive quality bonus."""
    category: Category
    name: str
    score: float  # Actual bonus points awarded
    max_score: float  # Maximum possible for this check
    description: str = ""


@dataclass
class Issue:
    """Represents an analysis issue."""
    severity: Severity
    category: Category
    subcategory: str
    message: str
    tick: int
    track: str = ""
    details: dict = field(default_factory=dict)

    @property
    def bar(self) -> int:
        return _tick_to_bar(self.tick)


@dataclass
class QualityScore:
    """Quality scores for music analysis with blueprint-aware weighting."""
    melodic: float = 100.0
    harmonic: float = 100.0
    rhythm: float = 100.0
    arrangement: float = 100.0
    structure: float = 100.0
    overall: float = 100.0
    details: Dict[str, float] = field(default_factory=dict)
    # Bonus fields (added to base scores, capped per category)
    melodic_bonus: float = 0.0
    harmonic_bonus: float = 0.0
    rhythm_bonus: float = 0.0
    structure_bonus: float = 0.0
    total_bonus: float = 0.0

    def calculate_overall(self, weights=None):
        """Calculate weighted overall score.

        Args:
            weights: Optional tuple of 5 floats
                     (melodic, harmonic, rhythm, arrangement, structure).
                     If None, uses default weights.
        """
        if weights:
            self.overall = (self.melodic * weights[0] +
                            self.harmonic * weights[1] +
                            self.rhythm * weights[2] +
                            self.arrangement * weights[3] +
                            self.structure * weights[4])
        else:
            self.overall = (self.melodic * 0.25 +
                            self.harmonic * 0.25 +
                            self.rhythm * 0.20 +
                            self.arrangement * 0.20 +
                            self.structure * 0.10)

    @property
    def grade(self) -> str:
        if self.overall >= 90:
            return 'A'
        if self.overall >= 80:
            return 'B'
        if self.overall >= 70:
            return 'C'
        if self.overall >= 60:
            return 'D'
        return 'F'


@dataclass
class HookPattern:
    """Detected hook/motif pattern."""
    start_bar: int
    end_bar: int
    pitches: List[int]
    rhythm: List[int]  # IOIs
    occurrences: List[int]  # start bars of each occurrence
    similarity: float = 0.0


@dataclass
class AnalysisResult:
    """Complete analysis result."""
    notes: List[Note]
    issues: List[Issue]
    score: QualityScore
    hooks: List[HookPattern] = field(default_factory=list)
    energy_curve: List[Tuple[int, float]] = field(default_factory=list)  # (bar, energy)
    metadata: dict = field(default_factory=dict)
    bonuses: List[Bonus] = field(default_factory=list)


@dataclass
class TestResult:
    """Result from batch testing."""
    seed: int
    style: int
    chord: int
    blueprint: int
    score: QualityScore = field(default_factory=QualityScore)
    error_count: int = 0
    warning_count: int = 0
    info_count: int = 0
    error: Optional[str] = None

    def cli_command(self) -> str:
        return (f"./build/bin/midisketch_cli --analyze "
                f"--seed {self.seed} --style {self.style} "
                f"--chord {self.chord} --blueprint {self.blueprint}")
