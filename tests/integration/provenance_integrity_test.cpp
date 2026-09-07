/**
 * @file provenance_integrity_test.cpp
 * @brief Verifies that a note's recorded harmonic context still describes it.
 *
 * A note records which chord degree was read and at which tick it was read.
 * Nothing in generation consults those fields, so a wrong value cannot be heard
 * -- it can only mislead whoever measures the output later. That is exactly
 * what makes it worth pinning: a pass that relocates a note and carries the old
 * degree along turns the record into a confident lie about music that is
 * perfectly fine, and the reading it produces is indistinguishable from a real
 * harmonic defect.
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "core/i_harmony_context.h"
#include "core/preset_data.h"
#include "core/preset_types.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "midisketch.h"

// Not verified in the shipping build, and nothing there needs it to be: the
// record whose honesty this file pins is absent from that build, so it cannot
// mislead anyone measuring its output.
#ifdef MIDISKETCH_NOTE_PROVENANCE

namespace midisketch {
namespace {

/// Source recorded by notes the post-generation pass places or rewrites.
constexpr uint8_t kPostProcessSource = 14;

TEST(ProvenanceIntegrityTest, APostGenerationNoteRecordsTheChordAtTheTickItNames) {
  // The post-generation pass runs after the chord timeline is settled, so
  // nothing can rewrite the harmony beneath its notes afterwards. For these
  // notes -- unlike the tracks generated before the chord track, which an
  // anticipation can legitimately overtake -- a recorded degree that the
  // harmony contradicts at the very tick the record names has only one cause:
  // the note was moved and its degree was not.
  size_t post_process_notes = 0;
  size_t at_a_mid_bar_change = 0;

  for (uint32_t seed = 1; seed <= 16; ++seed) {
    SongConfig config = createDefaultSongConfig(0);
    config.blueprint_id = 2;
    config.seed = seed;

    MidiSketch sketch;
    sketch.generateFromConfig(config);
    const IHarmonyContext& harmony = sketch.getHarmonyContext();

    for (size_t i = 0; i < kTrackCount; ++i) {
      const TrackRole role = static_cast<TrackRole>(i);
      if (role == TrackRole::Drums || role == TrackRole::SE) continue;

      for (const NoteEvent& note : sketch.getSong().tracks()[i].notes()) {
        if (note.prov_source != kPostProcessSource || note.prov_chord_degree < 0) continue;
        ++post_process_notes;

        const bool starts_a_new_chord =
            note.start_tick > 0 && harmony.getChordDegreeAt(note.start_tick - 1) !=
                                       harmony.getChordDegreeAt(note.start_tick);
        if (note.start_tick % TICKS_PER_BAR != 0 && starts_a_new_chord) {
          ++at_a_mid_bar_change;
        }

        EXPECT_EQ(harmony.getChordDegreeAt(note.prov_lookup_tick), note.prov_chord_degree)
            << "seed " << seed << ": a " << trackRoleToString(role) << " note at tick "
            << note.start_tick << " records degree " << static_cast<int>(note.prov_chord_degree)
            << " read at tick " << note.prov_lookup_tick << ", where the harmony has degree "
            << static_cast<int>(harmony.getChordDegreeAt(note.prov_lookup_tick));
      }
    }
  }

  // A note the pass places where the chord changes mid-bar is the case the
  // invariant exists for: it is placed at one context having been derived from
  // another. Without any in the corpus the loop above proves nothing.
  ASSERT_GT(post_process_notes, 0u) << "the post-generation pass placed no notes at all";
  ASSERT_GT(at_a_mid_bar_change, 0u)
      << "no post-generation note landed on a mid-bar chord change, so nothing here "
         "exercises a note derived from one context and placed in another";
}

}  // namespace
}  // namespace midisketch

#endif  // MIDISKETCH_NOTE_PROVENANCE
