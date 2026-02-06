"""Domain-specific music analyzers."""

from .base import BaseAnalyzer, BaseBonusAnalyzer
from .melodic import MelodicAnalyzer
from .vocal import VocalAnalyzer
from .harmonic import HarmonicAnalyzer
from .rhythm import RhythmAnalyzer
from .arrangement import ArrangementAnalyzer
from .structure import StructureAnalyzer
from .bonus_melodic import BonusMelodicAnalyzer
from .bonus_harmonic import BonusHarmonicAnalyzer
from .bonus_rhythm import BonusRhythmAnalyzer
from .bonus_structure import BonusStructureAnalyzer

__all__ = [
    'BaseAnalyzer',
    'BaseBonusAnalyzer',
    'MelodicAnalyzer',
    'VocalAnalyzer',
    'HarmonicAnalyzer',
    'RhythmAnalyzer',
    'ArrangementAnalyzer',
    'StructureAnalyzer',
    'BonusMelodicAnalyzer',
    'BonusHarmonicAnalyzer',
    'BonusRhythmAnalyzer',
    'BonusStructureAnalyzer',
]
