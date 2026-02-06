"""music_analyzer - Comprehensive music quality analysis for midi-sketch."""

from .constants import (
    TICKS_PER_BEAT, TICKS_PER_BAR, TRACK_NAMES, TRACK_CHANNELS,
    NOTE_NAMES, BLUEPRINT_NAMES, C_MAJOR_SCALE, DISSONANT_INTERVALS,
    CONSONANT_LEAPS, SEMI_CONSONANT_LEAPS, TRACK_RANGES,
    SECTION_LENGTH_BARS, Severity, Category,
)
from .models import Note, Issue, QualityScore, HookPattern, AnalysisResult, TestResult
from .blueprints import BlueprintProfile, BLUEPRINT_PROFILES
from .helpers import (
    note_name, tick_to_bar, tick_to_bar_beat,
    _edit_distance, _pattern_similarity, _ioi_entropy,
)
from .analyzer import MusicAnalyzer
from .formatter import OutputFormatter, apply_filters
from .loader import load_json_output, load_json_metadata
from .runner import run_single_test, run_batch_tests, print_batch_summary

__all__ = [
    'TICKS_PER_BEAT', 'TICKS_PER_BAR', 'TRACK_NAMES', 'TRACK_CHANNELS',
    'NOTE_NAMES', 'BLUEPRINT_NAMES', 'C_MAJOR_SCALE', 'DISSONANT_INTERVALS',
    'CONSONANT_LEAPS', 'SEMI_CONSONANT_LEAPS', 'TRACK_RANGES',
    'SECTION_LENGTH_BARS', 'Severity', 'Category',
    'Note', 'Issue', 'QualityScore', 'HookPattern', 'AnalysisResult', 'TestResult',
    'BlueprintProfile', 'BLUEPRINT_PROFILES',
    'note_name', 'tick_to_bar', 'tick_to_bar_beat',
    '_edit_distance', '_pattern_similarity', '_ioi_entropy',
    'MusicAnalyzer', 'OutputFormatter', 'apply_filters',
    'load_json_output', 'load_json_metadata',
    'run_single_test', 'run_batch_tests', 'print_batch_summary',
]
