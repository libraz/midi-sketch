"""Blueprint profile system for blueprint-aware quality scoring.

Each blueprint has different musical characteristics and expectations.
Weights control how much each category contributes to the overall score.
Tolerances adjust sensitivity to specific issue types.
"""

from dataclasses import dataclass


@dataclass
class BlueprintProfile:
    """Profile for blueprint-aware quality scoring.

    Each blueprint has different musical characteristics and expectations.
    Weights control how much each category contributes to the overall score.
    Tolerances adjust sensitivity to specific issue types.
    """
    name: str
    paradigm: str  # Traditional, RhythmSync, MelodyDriven
    riff_policy: str  # Free, Locked, Evolving
    weight_melodic: float = 0.25
    weight_harmonic: float = 0.25
    weight_rhythm: float = 0.20
    weight_arrangement: float = 0.20
    weight_structure: float = 0.10
    leap_tolerance: float = 1.0
    density_tolerance: float = 1.0
    rhythm_sync_required: bool = False
    motif_consistency_min: float = 0.5
    velocity_sensitivity: float = 1.0
    long_note_bonus: bool = False
    expected_climax_section: str = "chorus"  # "chorus" or "any"
    # Bonus scoring weights (1.0 = neutral)
    hook_bonus_weight: float = 1.0
    groove_bonus_weight: float = 1.0
    tension_bonus_weight: float = 1.0
    dynamics_bonus_weight: float = 1.0
    simplicity_bonus_weight: float = 1.0
    # Per-category bonus caps
    bonus_cap_melodic: float = 10.0
    bonus_cap_harmonic: float = 8.0
    bonus_cap_rhythm: float = 8.0
    bonus_cap_structure: float = 8.0


BLUEPRINT_PROFILES = {
    0: BlueprintProfile(
        "Traditional", "Traditional", "Free",
        weight_melodic=0.25, weight_harmonic=0.25, weight_rhythm=0.25,
        weight_arrangement=0.15, weight_structure=0.10,
        motif_consistency_min=0.3,
        expected_climax_section="any",
        hook_bonus_weight=1.0, groove_bonus_weight=1.0,
        tension_bonus_weight=1.0, dynamics_bonus_weight=1.0,
        simplicity_bonus_weight=1.0,
        bonus_cap_melodic=10.0,
    ),
    1: BlueprintProfile(
        "RhythmLock", "RhythmSync", "Locked",
        weight_melodic=0.20, weight_harmonic=0.20, weight_rhythm=0.35,
        weight_arrangement=0.15, weight_structure=0.10,
        rhythm_sync_required=True, motif_consistency_min=0.85,
        hook_bonus_weight=0.8, groove_bonus_weight=1.5,
        tension_bonus_weight=0.8, dynamics_bonus_weight=0.8,
        simplicity_bonus_weight=1.0,
        bonus_cap_melodic=8.0,
    ),
    2: BlueprintProfile(
        "StoryPop", "MelodyDriven", "Evolving",
        weight_melodic=0.30, weight_harmonic=0.25, weight_rhythm=0.20,
        weight_arrangement=0.15, weight_structure=0.10,
        motif_consistency_min=0.5, leap_tolerance=1.2,
        hook_bonus_weight=1.3, groove_bonus_weight=0.8,
        tension_bonus_weight=1.3, dynamics_bonus_weight=1.2,
        simplicity_bonus_weight=0.8,
        bonus_cap_melodic=12.0,
    ),
    3: BlueprintProfile(
        "Ballad", "MelodyDriven", "Free",
        weight_melodic=0.30, weight_harmonic=0.25, weight_rhythm=0.15,
        weight_arrangement=0.15, weight_structure=0.15,
        density_tolerance=0.6, long_note_bonus=True, velocity_sensitivity=1.5,
        hook_bonus_weight=1.0, groove_bonus_weight=0.5,
        tension_bonus_weight=1.5, dynamics_bonus_weight=1.5,
        simplicity_bonus_weight=1.2,
        bonus_cap_melodic=10.0,
    ),
    4: BlueprintProfile(
        "IdolStandard", "MelodyDriven", "Evolving",
        weight_melodic=0.30, weight_harmonic=0.25, weight_rhythm=0.20,
        weight_arrangement=0.15, weight_structure=0.10,
        motif_consistency_min=0.5, leap_tolerance=1.3,
        hook_bonus_weight=1.5, groove_bonus_weight=1.0,
        tension_bonus_weight=1.0, dynamics_bonus_weight=1.0,
        simplicity_bonus_weight=1.0,
        bonus_cap_melodic=12.0,
    ),
    5: BlueprintProfile(
        "IdolHyper", "RhythmSync", "Locked",
        weight_melodic=0.20, weight_harmonic=0.20, weight_rhythm=0.35,
        weight_arrangement=0.15, weight_structure=0.10,
        density_tolerance=1.5, rhythm_sync_required=True,
        motif_consistency_min=0.85, leap_tolerance=1.5,
        hook_bonus_weight=0.8, groove_bonus_weight=1.5,
        tension_bonus_weight=0.5, dynamics_bonus_weight=0.5,
        simplicity_bonus_weight=0.8,
        bonus_cap_melodic=8.0,
    ),
    6: BlueprintProfile(
        "IdolKawaii", "MelodyDriven", "Locked",
        weight_melodic=0.30, weight_harmonic=0.25, weight_rhythm=0.20,
        weight_arrangement=0.15, weight_structure=0.10,
        leap_tolerance=0.7, motif_consistency_min=0.8,
        hook_bonus_weight=1.2, groove_bonus_weight=0.8,
        tension_bonus_weight=0.8, dynamics_bonus_weight=0.5,
        simplicity_bonus_weight=1.5,
        bonus_cap_melodic=10.0,
    ),
    7: BlueprintProfile(
        "IdolCoolPop", "RhythmSync", "Locked",
        weight_melodic=0.25, weight_harmonic=0.20, weight_rhythm=0.30,
        weight_arrangement=0.15, weight_structure=0.10,
        rhythm_sync_required=True, motif_consistency_min=0.85,
        leap_tolerance=1.3,
        hook_bonus_weight=1.0, groove_bonus_weight=1.5,
        tension_bonus_weight=1.0, dynamics_bonus_weight=1.2,
        simplicity_bonus_weight=0.8,
        bonus_cap_melodic=8.0,
    ),
    8: BlueprintProfile(
        "IdolEmo", "MelodyDriven", "Locked",
        weight_melodic=0.30, weight_harmonic=0.25, weight_rhythm=0.15,
        weight_arrangement=0.15, weight_structure=0.15,
        velocity_sensitivity=1.5, motif_consistency_min=0.8,
        leap_tolerance=1.2,
        hook_bonus_weight=1.2, groove_bonus_weight=0.5,
        tension_bonus_weight=1.5, dynamics_bonus_weight=1.5,
        simplicity_bonus_weight=1.0,
        bonus_cap_melodic=12.0,
    ),
}
