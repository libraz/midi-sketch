/**
 * @file collision_test_helper.h
 * @brief Helper class for collision analysis in tests.
 *
 * Provides structured APIs for analyzing collision state and verifying
 * that generation produces clash-free output.
 */

#ifndef MIDISKETCH_TEST_COLLISION_TEST_HELPER_H
#define MIDISKETCH_TEST_COLLISION_TEST_HELPER_H

#include <sstream>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "core/i_harmony_context.h"

namespace midisketch {
namespace test {

/**
 * @brief Helper class for collision analysis in tests.
 *
 * Wraps IHarmonyContext to provide convenient test APIs:
 * - Get collision snapshots at specific ticks
 * - Find all clashes in a time range
 * - Filter clashes by track pair
 * - Format snapshots for test output
 *
 * Usage:
 * @code
 * CollisionTestHelper helper(gen.getHarmonyContext());
 * auto snapshot = helper.snapshotAt(105600);
 * EXPECT_TRUE(snapshot.clashes.empty());
 *
 * auto all_clashes = helper.findAllClashes();
 * EXPECT_EQ(all_clashes.size(), 0);
 * @endcode
 */
class CollisionTestHelper {
 public:
  /**
   * @brief Construct a helper with a harmony context.
   * @param harmony Reference to IHarmonyContext (typically from Generator)
   */
  explicit CollisionTestHelper(const IHarmonyContext& harmony) : harmony_(harmony) {}

  /**
   * @brief Get collision snapshot at a specific tick.
   * @param tick Position to analyze
   * @param range Range around tick to include (default: 1 bar)
   * @return CollisionSnapshot with notes and clashes
   */
  CollisionSnapshot snapshotAt(Tick tick, Tick range = 1920) const {
    return harmony_.getCollisionSnapshot(tick, range);
  }

  /**
   * @brief Find all clashes in the entire song.
   * @param tick_step Step size for scanning (default: 240 = 1 beat)
   * @param total_ticks Total song length in ticks
   * @return Vector of all clashes found
   */
  std::vector<ClashDetail> findAllClashes(Tick total_ticks, Tick tick_step = 240) const {
    std::vector<ClashDetail> all_clashes;

    for (Tick tick = 0; tick < total_ticks; tick += tick_step) {
      auto snapshot = snapshotAt(tick, tick_step);
      for (const auto& clash : snapshot.clashes) {
        // Avoid duplicates (same notes at overlapping positions)
        bool is_duplicate = false;
        for (const auto& existing : all_clashes) {
          if (existing.note_a.start == clash.note_a.start &&
              existing.note_a.pitch == clash.note_a.pitch &&
              existing.note_b.start == clash.note_b.start &&
              existing.note_b.pitch == clash.note_b.pitch) {
            is_duplicate = true;
            break;
          }
        }
        if (!is_duplicate) {
          all_clashes.push_back(clash);
        }
      }
    }

    return all_clashes;
  }

  /**
   * @brief Find clashes between specific track pairs.
   * @param track_a First track role
   * @param track_b Second track role
   * @param total_ticks Total song length in ticks
   * @param tick_step Step size for scanning (default: 240 = 1 beat)
   * @return Vector of clashes between the specified tracks
   */
  std::vector<ClashDetail> findClashesBetween(TrackRole track_a, TrackRole track_b,
                                               Tick total_ticks, Tick tick_step = 240) const {
    auto all = findAllClashes(total_ticks, tick_step);
    std::vector<ClashDetail> filtered;

    for (const auto& clash : all) {
      bool match_a = (clash.note_a.track == track_a && clash.note_b.track == track_b);
      bool match_b = (clash.note_a.track == track_b && clash.note_b.track == track_a);
      if (match_a || match_b) {
        filtered.push_back(clash);
      }
    }

    return filtered;
  }

  /**
   * @brief Format a snapshot for test output.
   * @param snapshot CollisionSnapshot to format
   * @return Human-readable string representation
   */
  static std::string formatSnapshot(const CollisionSnapshot& snapshot) {
    std::ostringstream oss;
    oss << "=== Collision Snapshot at tick " << snapshot.tick << " ===\n";
    oss << "Range: [" << snapshot.range_start << ", " << snapshot.range_end << ")\n";
    oss << "Notes in range: " << snapshot.notes_in_range.size() << "\n";
    oss << "Sounding notes: " << snapshot.sounding_notes.size() << "\n";
    oss << "Clashes: " << snapshot.clashes.size() << "\n";

    if (!snapshot.clashes.empty()) {
      oss << "\nClash details:\n";
      for (const auto& clash : snapshot.clashes) {
        oss << "  " << trackRoleToString(clash.note_a.track)
            << "(pitch=" << static_cast<int>(clash.note_a.pitch) << ")"
            << " vs " << trackRoleToString(clash.note_b.track)
            << "(pitch=" << static_cast<int>(clash.note_b.pitch) << ")"
            << " = " << clash.interval_name
            << " (" << clash.interval_semitones << " semitones)\n";
      }
    }

    return oss.str();
  }

  /**
   * @brief Format a clash for test output.
   * @param clash ClashDetail to format
   * @return Human-readable string representation
   */
  static std::string formatClash(const ClashDetail& clash) {
    std::ostringstream oss;
    oss << trackRoleToString(clash.note_a.track)
        << "(pitch=" << static_cast<int>(clash.note_a.pitch)
        << ", tick=" << clash.note_a.start << "-" << clash.note_a.end << ")"
        << " vs " << trackRoleToString(clash.note_b.track)
        << "(pitch=" << static_cast<int>(clash.note_b.pitch)
        << ", tick=" << clash.note_b.start << "-" << clash.note_b.end << ")"
        << " = " << clash.interval_name
        << " (" << clash.interval_semitones << " semitones)";
    return oss.str();
  }

 private:
  const IHarmonyContext& harmony_;
};

}  // namespace test
}  // namespace midisketch

#endif  // MIDISKETCH_TEST_COLLISION_TEST_HELPER_H
