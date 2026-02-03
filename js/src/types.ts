/**
 * Types and interfaces for midi-sketch
 */

// ============================================================================
// Song Configuration Types
// ============================================================================

/**
 * Song configuration for style-based generation
 */
export interface SongConfig {
  // Basic settings
  /** Style preset ID */
  stylePresetId: number;
  /** Key (0-11) */
  key: number;
  /** BPM (0 = use style default) */
  bpm: number;
  /** Random seed (0 = random) */
  seed: number;
  /** Chord progression ID */
  chordProgressionId: number;
  /** Form/structure pattern ID */
  formId: number;
  /** Vocal attitude: 0=Clean, 1=Expressive, 2=Raw */
  vocalAttitude: number;
  /** Enable drums */
  drumsEnabled: boolean;
  /** Blueprint ID: 0=Traditional, 1=Orangestar, 2=YOASOBI, 3=Ballad, 255=random */
  blueprintId: number;

  // Arpeggio settings
  /** Enable arpeggio */
  arpeggioEnabled: boolean;
  /** Arpeggio pattern: 0=Up, 1=Down, 2=UpDown, 3=Random */
  arpeggioPattern: number;
  /** Arpeggio speed: 0=Eighth, 1=Sixteenth, 2=Triplet */
  arpeggioSpeed: number;
  /** Arpeggio octave range (1-3) */
  arpeggioOctaveRange: number;
  /** Arpeggio gate length (0-100) */
  arpeggioGate: number;

  // Vocal settings
  /** Vocal range lower bound (MIDI note) */
  vocalLow: number;
  /** Vocal range upper bound (MIDI note) */
  vocalHigh: number;
  /** Skip vocal generation (for BGM-first workflow) */
  skipVocal: boolean;

  // Humanization
  /** Enable humanization */
  humanize: boolean;
  /** Timing variation (0-100) */
  humanizeTiming: number;
  /** Velocity variation (0-100) */
  humanizeVelocity: number;

  // Chord extensions
  /** Enable sus2/sus4 chords */
  chordExtSus: boolean;
  /** Enable 7th chords */
  chordExt7th: boolean;
  /** Enable 9th chords */
  chordExt9th: boolean;
  /** Sus chord probability (0-100) */
  chordExtSusProb: number;
  /** 7th chord probability (0-100) */
  chordExt7thProb: number;
  /** 9th chord probability (0-100) */
  chordExt9thProb: number;

  // Composition style
  /** Composition style: 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven */
  compositionStyle: number;

  // Duration
  /** Target duration in seconds (0 = use formId) */
  targetDurationSeconds: number;

  // Modulation settings
  /** Modulation timing: 0=None, 1=LastChorus, 2=AfterBridge, 3=EachChorus, 4=Random */
  modulationTiming: number;
  /** Modulation semitones (+1 to +4) */
  modulationSemitones: number;

  // SE/Call settings
  /** Enable SE track */
  seEnabled: boolean;
  /** Enable call feature (maps to call_setting: false=Auto(0), true=Enabled(1)) */
  callEnabled: boolean;
  /** Output calls as notes */
  callNotesEnabled: boolean;
  /** Intro chant: 0=None, 1=Gachikoi, 2=Shouting */
  introChant: number;
  /** Mix pattern: 0=None, 1=Standard, 2=Tiger */
  mixPattern: number;
  /** Call density: 0=None, 1=Minimal, 2=Standard, 3=Intense */
  callDensity: number;

  // Vocal style settings
  /** Vocal style preset: 0=Auto, 1=Standard, 2=Vocaloid, etc. */
  vocalStyle: number;
  /** Melody template: 0=Auto, 1=PlateauTalk, 2=RunUpTarget, etc. */
  melodyTemplate: number;

  // Arrangement settings
  /** Arrangement growth: 0=LayerAdd, 1=RegisterAdd */
  arrangementGrowth: number;

  // Arpeggio sync settings
  /** Sync arpeggio with chord changes (default=true) */
  arpeggioSyncChord: boolean;

  // Motif settings (for BackgroundMotif style)
  /** Motif repeat scope: 0=FullSong, 1=Section */
  motifRepeatScope: number;
  /** Same progression for all sections (default=true) */
  motifFixedProgression: boolean;
  /** Max chord count (0=no limit, 2-8) */
  motifMaxChordCount: number;

  // Melodic complexity and hook control
  /** Melodic complexity: 0=Simple, 1=Standard, 2=Complex */
  melodicComplexity: number;
  /** Hook intensity: 0=Off, 1=Light, 2=Normal, 3=Strong */
  hookIntensity: number;
  /** Vocal groove feel: 0=Straight, 1=OffBeat, 2=Swing, 3=Syncopated, 4=Driving16th, 5=Bouncy8th */
  vocalGroove: number;

  // Mood override
  /** Mood preset override (0-23, used when moodExplicit=true) */
  mood: number;
  /** 0=derive from style, 1=use mood field */
  moodExplicit: boolean;

  // Form control
  /** 0=may randomize, 1=use formId exactly */
  formExplicit: boolean;

  // Drive and addictive
  /** Drive feel: 0=laid-back, 50=neutral, 100=aggressive */
  driveFeel: number;
  /** Enable Behavioral Loop mode (fixed riff, maximum hook) */
  addictiveMode: boolean;
}

/**
 * Note input for custom vocal track
 */
export interface NoteInput {
  /** Note start time in ticks */
  startTick: number;
  /** Note duration in ticks */
  duration: number;
  /** MIDI note number (0-127) */
  pitch: number;
  /** Note velocity (0-127) */
  velocity: number;
}

// ============================================================================
// Vocal and Accompaniment Configuration Types
// ============================================================================

/**
 * Vocal regeneration configuration
 */
export interface VocalConfig {
  /** Random seed (0 = new random) */
  seed?: number;
  /** Vocal range lower bound (MIDI note, 36-96) */
  vocalLow?: number;
  /** Vocal range upper bound (MIDI note, 36-96) */
  vocalHigh?: number;
  /** Vocal attitude: 0=Clean, 1=Expressive, 2=Raw */
  vocalAttitude?: number;
  /** Vocal style preset: 0=Auto, 1=Standard, 2=Vocaloid, etc. */
  vocalStyle?: number;
  /** Melody template: 0=Auto, 1=PlateauTalk, 2=RunUpTarget, etc. */
  melodyTemplate?: number;
  /** Melodic complexity: 0=Simple, 1=Standard, 2=Complex */
  melodicComplexity?: number;
  /** Hook intensity: 0=Off, 1=Light, 2=Normal, 3=Strong */
  hookIntensity?: number;
  /** Vocal groove feel: 0=Straight, 1=OffBeat, 2=Swing, etc. */
  vocalGroove?: number;
  /** Composition style: 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven */
  compositionStyle?: number;
}

/**
 * Configuration for accompaniment generation/regeneration.
 */
export interface AccompanimentConfig {
  /** Random seed for BGM (0 = auto-generate) */
  seed?: number;

  // Drums
  /** Enable drums */
  drumsEnabled?: boolean;

  // Arpeggio
  /** Enable arpeggio */
  arpeggioEnabled?: boolean;
  /** Arpeggio pattern: 0=Up, 1=Down, 2=UpDown, 3=Random */
  arpeggioPattern?: number;
  /** Arpeggio speed: 0=Eighth, 1=Sixteenth, 2=Triplet */
  arpeggioSpeed?: number;
  /** Arpeggio octave range: 1-3 */
  arpeggioOctaveRange?: number;
  /** Arpeggio gate length: 0-100 */
  arpeggioGate?: number;
  /** Sync arpeggio with chord changes */
  arpeggioSyncChord?: boolean;

  // Chord Extensions
  /** Enable sus chord extension */
  chordExtSus?: boolean;
  /** Enable 7th chord extension */
  chordExt7th?: boolean;
  /** Enable 9th chord extension */
  chordExt9th?: boolean;
  /** Sus probability: 0-100 */
  chordExtSusProb?: number;
  /** 7th probability: 0-100 */
  chordExt7thProb?: number;
  /** 9th probability: 0-100 */
  chordExt9thProb?: number;

  // Humanization
  /** Enable humanization */
  humanize?: boolean;
  /** Timing variation: 0-100 */
  humanizeTiming?: number;
  /** Velocity variation: 0-100 */
  humanizeVelocity?: number;

  // SE
  /** Enable SE track */
  seEnabled?: boolean;

  // Call System
  /** Enable call system */
  callEnabled?: boolean;
  /** Call density: 0=Sparse, 1=Light, 2=Standard, 3=Dense */
  callDensity?: number;
  /** Intro chant: 0=None, 1=Gachikoi, 2=Mix */
  introChant?: number;
  /** Mix pattern: 0=None, 1=Standard, 2=Tiger */
  mixPattern?: number;
  /** Output call as MIDI notes */
  callNotesEnabled?: boolean;
}

// ============================================================================
// Piano Roll Safety Types
// ============================================================================

/**
 * Note safety level for piano roll visualization
 */
export const NoteSafety = {
  /** Green: chord tone, safe to use */
  Safe: 0,
  /** Yellow: tension, low register, or passing tone */
  Warning: 1,
  /** Red: dissonant or out of range */
  Dissonant: 2,
} as const;

export type NoteSafetyLevel = (typeof NoteSafety)[keyof typeof NoteSafety];

/**
 * Reason flags for note safety (bitfield, can be combined)
 */
export const NoteReason = {
  None: 0,
  // Positive reasons (green)
  ChordTone: 1, // Chord tone (root, 3rd, 5th, 7th)
  Tension: 2, // Tension (9th, 11th, 13th)
  ScaleTone: 4, // Scale tone (not chord but in scale)
  // Warning reasons (yellow)
  LowRegister: 8, // Low register (below C4), may sound muddy
  Tritone: 16, // Tritone interval (unstable except on V7)
  LargeLeap: 32, // Large leap (6+ semitones from prev note)
  // Dissonant reasons (red)
  Minor2nd: 64, // Minor 2nd (1 semitone) collision
  Major7th: 128, // Major 7th (11 semitones) collision
  NonScale: 256, // Non-scale tone (chromatic)
  PassingTone: 512, // Can be used as passing tone
  // Out of range reasons (red)
  OutOfRange: 1024, // Outside vocal range
  TooHigh: 2048, // Too high to sing
  TooLow: 4096, // Too low to sing
} as const;

export type NoteReasonFlags = number;

/**
 * Collision info for a note that collides with BGM
 */
export interface CollisionInfo {
  /** Track role of colliding track */
  trackRole: number;
  /** MIDI pitch of colliding note */
  collidingPitch: number;
  /** Collision interval in semitones (1, 6, or 11) */
  intervalSemitones: number;
}

/**
 * Piano roll safety info for a single tick
 */
export interface PianoRollInfo {
  /** Tick position */
  tick: number;
  /** Current chord degree (0=I, 1=ii, etc.) */
  chordDegree: number;
  /** Current key (0-11, considering modulation) */
  currentKey: number;
  /** Safety level for each MIDI note (0-127) */
  safety: NoteSafetyLevel[];
  /** Reason flags for each note (0-127) */
  reason: NoteReasonFlags[];
  /** Collision details for each note */
  collision: CollisionInfo[];
  /** Recommended notes (priority order, max 8) */
  recommended: number[];
}

// ============================================================================
// Preset Types
// ============================================================================

/**
 * Preset information
 */
export interface PresetInfo {
  /** Preset name */
  name: string;
  /** Display string (for chords) */
  display?: string;
  /** Default BPM (for moods) */
  defaultBpm?: number;
}

/**
 * Style preset information
 */
export interface StylePresetInfo {
  /** Style preset ID */
  id: number;
  /** Internal name */
  name: string;
  /** Display name */
  displayName: string;
  /** Description */
  description: string;
  /** Default tempo */
  tempoDefault: number;
  /** Bit flags for allowed vocal attitudes */
  allowedAttitudes: number;
}

/**
 * Event data from generation
 */
export interface EventData {
  bpm: number;
  division: number;
  duration_ticks: number;
  duration_seconds: number;
  tracks: Array<{
    name: string;
    channel: number;
    program: number;
    notes: Array<{
      pitch: number;
      velocity: number;
      start_ticks: number;
      duration_ticks: number;
      start_seconds: number;
      duration_seconds: number;
    }>;
  }>;
  sections: Array<{
    name: string;
    type: string;
    startTick: number;
    endTick: number;
    start_bar: number;
    bars: number;
    start_seconds: number;
    end_seconds: number;
  }>;
}
