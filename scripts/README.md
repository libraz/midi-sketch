# `scripts/` — analysis and verification tooling

Python and Node helpers used to judge generation quality. They read the events JSON the
generator writes next to its MIDI — `output.json` by default, or `<file>.json` when the
CLI is given `-o <file>` — and, for the comparison tools, standard MIDI files. The JSON is
written on every generation; `--json` only switches the CLI's own report to
machine-readable form and does not produce it.

Run everything from the repository root unless stated otherwise.

## Music analyzer (`music_analyzer/`)

Covers melody, rhythm, arrangement and structure, which the C++ `--analyze` flag does
not. Its harmonic dissonance check classifies the same five interval classes as
`--analyze` (m2, M2, tritone, M7, m9) with the same severities, so on generated material
it re-verifies the generator's own collision contract rather than finding new clashes;
the checks that can find something the generator did not rule out are the ones asking a
different question, such as a pair that only turns dissonant once the chord changes
under it.

| Goal | Command |
|---|---|
| Quality score at a glance | `python3 scripts/music_analyzer.py output.json --quick` |
| One track in detail | `python3 scripts/music_analyzer.py output.json --track Chord` |
| One bar range | `python3 scripts/music_analyzer.py output.json --bar-range 15-20` |
| Harmony findings only | `python3 scripts/music_analyzer.py output.json --category harmonic` |
| Generate and analyze in one step | `python3 scripts/music_analyzer.py --generate --seed 42 --bp 1` |
| Batch sweep | `python3 scripts/music_analyzer.py --batch --seeds 20 -j 4` |

Output modes: `--quick` (one-line summary plus errors and warnings), `--json` (for
scripting), `--score-only`, `--track X`, or the default full report. `--track` takes any
track name — Vocal, Chord, Bass, Motif, Arpeggio, Aux, Guitar, Drums, SE — and matches it
as a case-insensitive substring, so a cross-track finding such as `Guitar/Bass` is
reachable from either name.

### What each category detects

These are the values `--category` accepts. Vocal findings are not a category of their
own: they are filed under `melodic` (and `structure` for section contrast).

| Category | Detects |
|---|---|
| melodic | Isolated notes, runs of 6 or more consecutive same pitches (8 or more is an error), range violations, unresolved leaps wider than 14 semitones, monotone contour, melodic arc, plus the vocal checks: breathability, tessitura, climax placement, interval distribution |
| harmonic | Dissonance (m2, M2, tritone, M7, m9), one- and two-voice chords, accompaniment above the vocal, consecutive identical voicings, bass monotony, harmonic function |
| rhythm | Excessive density, rhythmic monotony, beat-grid deviation, weak downbeats, backbeat, rhythmic diversity |
| arrangement | Register overlap, track separation, motif consistency/interference/density, rhythm sync |
| structure | Short and long phrases, track dominance, empty tracks, energy contrast, vocal section contrast |

### Melody discipline layers

Two melody checks are measured against a reference corpus rather than against fixed
rules: Layer 1 (corpus-universal prohibitions, applied to every blueprint) and Layer 2
(per-genre melody ranges, applied only when the blueprint identifies the genre). Both
read the target profile `build_reference_targets.py` writes next to the corpus. To read it
from elsewhere:

```bash
export MIDISKETCH_MELODY_TARGETS=/path/to/target_profiles.json
# or per run
python3 scripts/music_analyzer.py output.json --melody-targets /path/to/target_profiles.json
```

When the profile cannot be read the report says the layers did not run
(`melody_discipline_inactive`) instead of passing the song in silence. An unknown
blueprint still skips Layer 2 only; no genre-uniform default is ever substituted.

### Quality score

```
Overall = Melodic(25%) + Harmonic(25%) + Rhythm(25%) + Arrangement(15%) + Structure(10%)
Grade:    A(90+)  B(80-89)  C(70-79)  D(60-69)  F(<60)
```

The weights above are Blueprint 0's. Each blueprint has its own weighting, and the
analyzer reads the blueprint from the `metadata` block of `output.json`. Scoring a file
that has no blueprint metadata silently falls back to Blueprint 0 and produces a
different, misleading score — pass `--blueprint-single N` (or generate through
`--generate --bp N`) whenever the blueprint matters.

### Python tests

```bash
cd scripts && python3 -m unittest discover -s tests -v
```

## Reference-MIDI comparison

Compares generated output against real J-pop and vocaloid arrangements for rhythm
density, motif style and arrangement shape.

The reference corpus lives in a local-only `backup/reference/` directory (eight MIDI
files with their own `README.md` covering per-file format, PPQ, tempo, time signature
and analysis caveats). It is not distributed with the repository, so the tools below all
take explicit paths. `track_roles.json` in that directory maps each `(track, channel)`
pair to a musical role and to the closest midi-sketch track role (`ms_role`); prefer it
over heuristic track selection.

| Tool | Purpose |
|---|---|
| `compare_midi_profile.py` | Profiles a MIDI file: tempo, note density, pitch range, rhythm grid, vertical sonority. Ships its own SMF parser (`parse_smf`), so it has no third-party dependencies |
| `reference_motif_report.py` | Motif and rhythm profile comparison: onset grid, pulse ratio, repeat-cell consistency, chord-tone focus, lead interference. Accepts both `.mid` and generated `.json`, auto-loads `track_roles.json` labels (the vocal is skyline-filtered as the lead), `--role riff,bass` filters by role, `--heuristic` forces legacy track selection |

```bash
python3 scripts/compare_midi_profile.py <corpus>/henceforth.mid
python3 scripts/reference_motif_report.py <corpus>/henceforth.mid output.json --role riff,arpeggio
```

The C++ CLI also analyzes external MIDI directly: `./build/bin/midisketch_cli --input <file>.mid`.

### Target profiles and gap analysis

`build_reference_targets.py` aggregates the labeled reference tracks into per-category
target ranges (min/median/max of density, cell consistency, grid ratios, lead
interference) and writes `target_profiles.json` next to the corpus. Categories map
blueprint IDs to rhythmsync/idol/ballad/pop and are defined under `_blueprint_categories`
in `track_roles.json`. Re-run it after adding or relabeling references.

`compare_generation_to_targets.py [--seeds N] [--category C] [--json]` generates songs
per category (a representative blueprint across several seeds) and reports the per-role
metrics that fall outside the reference range. It prints the median across seeds, so
check per-seed values before declaring a gap closed.

Known measurement artifact: `eighth_grid_ratio` HIGH verdicts are not real findings. The
references are humanized performances while generation is quantized, so this metric is
structurally biased and should be ignored.

## Standalone checks

| Script | Purpose |
|---|---|
| `check_dissonance.py` | Sweeps seeds and configs looking for dissonance |
| `check_pitch_crossing.py` | Detects Chord/Motif/Arpeggio/Aux crossing above the vocal |
| `check_rhythmlock.py` | RhythmLock-focused dissonance testing (Motif-axis clashes) |
| `check_readme_examples.py` | Compiles and type-checks the quick-start examples in both top-level READMEs |
| `profile_generation.py` | Generation performance profiling; `--sample` attaches the macOS sampler |
| `verify_wasm_cli.py` | Verifies that the WASM build and the native CLI produce identical output |
| `validate_melodic_rules.py` | Corpus violation table for the melodic rule set; re-run after adding references |

A measurement script that reads `output.json` after a *failed* CLI run silently measures
the previous case. Always check the return code before trusting a number.
