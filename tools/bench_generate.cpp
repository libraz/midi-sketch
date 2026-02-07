/**
 * @file bench_generate.cpp
 * @brief Benchmark binary for profiling generation performance.
 *
 * Usage:
 *   ./build/bin/bench_generate              # Default: 50 seeds × 9 blueprints
 *   ./build/bin/bench_generate --seeds 100  # More seeds
 *   ./build/bin/bench_generate --bp 1       # Single blueprint
 *   ./build/bin/bench_generate --wait       # Wait before starting (for sample attach)
 */

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <unistd.h>
#include <vector>

#include "core/config_converter.h"
#include "core/generator.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"

using Clock = std::chrono::high_resolution_clock;

struct TimingResult {
  int blueprint;
  int seed;
  double elapsed_ms;
  int total_notes;
};

int main(int argc, char* argv[]) {
  int num_seeds = 50;
  int single_bp = -1;
  bool wait_mode = false;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
      num_seeds = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--bp") == 0 && i + 1 < argc) {
      single_bp = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--wait") == 0) {
      wait_mode = true;
    } else if (std::strcmp(argv[i], "--help") == 0) {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "  --seeds N   Number of seeds per blueprint (default: 50)\n"
                << "  --bp N      Single blueprint ID to test (-1=all, default: -1)\n"
                << "  --wait      Wait for keypress before starting (for sample attach)\n";
      return 0;
    }
  }

  std::vector<int> blueprints;
  if (single_bp >= 0) {
    blueprints.push_back(single_bp);
  } else {
    for (int i = 0; i <= 8; ++i) blueprints.push_back(i);
  }

  int total = num_seeds * static_cast<int>(blueprints.size());
  std::cout << "Benchmark: " << total << " generations ("
            << num_seeds << " seeds x " << blueprints.size()
            << " blueprints)\n";

  if (wait_mode) {
    std::cout << "PID: " << getpid() << "\n";
    std::cout << "Press Enter to start (attach sample profiler now)...\n";
    std::cin.get();
  }

  std::vector<TimingResult> results;
  results.reserve(total);

  int count = 0;
  auto bench_start = Clock::now();

  for (int bp : blueprints) {
    for (int seed = 1; seed <= num_seeds; ++seed) {
      ++count;

      midisketch::SongConfig config{};
      config.style_preset_id = 0;
      config.blueprint_id = static_cast<uint8_t>(bp);
      config.seed = static_cast<uint32_t>(seed);

      midisketch::GeneratorParams params =
          midisketch::ConfigConverter::convert(config);
      midisketch::Generator gen;

      auto t0 = Clock::now();
      gen.generate(params);
      auto t1 = Clock::now();

      double elapsed_ms =
          std::chrono::duration<double, std::milli>(t1 - t0).count();

      const auto& song = gen.getSong();
      int total_notes = 0;
      for (size_t i = 0; i < midisketch::kTrackCount; ++i) {
        auto role = static_cast<midisketch::TrackRole>(i);
        total_notes += static_cast<int>(song.track(role).notes().size());
      }

      results.push_back({bp, seed, elapsed_ms, total_notes});

      if (count % 50 == 0 || count == total) {
        std::cout << "  [" << count << "/" << total << "] bp=" << bp
                  << " seed=" << seed << " elapsed=" << std::fixed
                  << std::setprecision(1) << elapsed_ms << "ms"
                  << " notes=" << total_notes << "\n";
      }
    }
  }

  auto bench_end = Clock::now();
  double total_ms =
      std::chrono::duration<double, std::milli>(bench_end - bench_start)
          .count();

  // === Report ===
  std::cout << "\n" << std::string(70, '=') << "\n";
  std::cout << "GENERATION BENCHMARK RESULTS\n";
  std::cout << std::string(70, '=') << "\n\n";

  std::vector<double> all_times;
  for (const auto& r : results) all_times.push_back(r.elapsed_ms);
  std::sort(all_times.begin(), all_times.end());

  double sum = std::accumulate(all_times.begin(), all_times.end(), 0.0);
  double avg = sum / all_times.size();
  double med = all_times[all_times.size() / 2];

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  Total wall time:    " << total_ms << " ms\n";
  std::cout << "  Total generations:  " << results.size() << "\n";
  std::cout << "  Throughput:         "
            << (results.size() / (total_ms / 1000.0)) << " gen/s\n";
  std::cout << "  Mean:               " << avg << " ms\n";
  std::cout << "  Median:             " << med << " ms\n";
  std::cout << "  Min:                " << all_times.front() << " ms\n";
  std::cout << "  Max:                " << all_times.back() << " ms\n";

  size_t p95_idx = static_cast<size_t>(all_times.size() * 0.95);
  size_t p99_idx = static_cast<size_t>(all_times.size() * 0.99);
  std::cout << "  P95:                " << all_times[p95_idx] << " ms\n";
  std::cout << "  P99:                " << all_times[p99_idx] << " ms\n";

  // Per-blueprint stats
  const char* bp_names[] = {"Traditional", "RhythmLock", "StoryPop",
                            "Ballad",      "IdolStandard", "IdolHyper",
                            "IdolKawaii",  "IdolCoolPop",  "IdolEmo"};

  std::cout << "\n  " << std::left << std::setw(18) << "Blueprint"
            << std::right << std::setw(8) << "Mean" << std::setw(8) << "Med"
            << std::setw(8) << "Max" << std::setw(8) << "P95"
            << std::setw(8) << "Notes" << "\n";
  std::cout << "  " << std::string(58, '-') << "\n";

  for (int bp : blueprints) {
    std::vector<double> bp_times;
    std::vector<int> bp_notes;
    for (const auto& r : results) {
      if (r.blueprint == bp) {
        bp_times.push_back(r.elapsed_ms);
        bp_notes.push_back(r.total_notes);
      }
    }
    std::sort(bp_times.begin(), bp_times.end());

    double bp_avg =
        std::accumulate(bp_times.begin(), bp_times.end(), 0.0) /
        bp_times.size();
    double bp_med = bp_times[bp_times.size() / 2];
    double bp_max = bp_times.back();
    double bp_p95 = bp_times[static_cast<size_t>(bp_times.size() * 0.95)];
    double avg_notes =
        std::accumulate(bp_notes.begin(), bp_notes.end(), 0.0) /
        bp_notes.size();

    const char* name = (bp >= 0 && bp <= 8) ? bp_names[bp] : "Unknown";
    std::cout << "  " << std::left << std::setw(18) << name << std::right
              << std::fixed << std::setprecision(1) << std::setw(7) << bp_avg
              << std::setw(8) << bp_med << std::setw(8) << bp_max
              << std::setw(8) << bp_p95 << std::setw(8)
              << static_cast<int>(avg_notes) << "\n";
  }

  // Slowest 10
  std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) {
              return a.elapsed_ms > b.elapsed_ms;
            });
  std::cout << "\n  Top 10 slowest:\n";
  for (int i = 0; i < std::min(10, static_cast<int>(results.size())); ++i) {
    const auto& r = results[i];
    const char* name =
        (r.blueprint >= 0 && r.blueprint <= 8) ? bp_names[r.blueprint]
                                                : "Unknown";
    std::cout << "    " << std::fixed << std::setprecision(1) << std::setw(7)
              << r.elapsed_ms << "ms  " << name << " seed=" << r.seed
              << " notes=" << r.total_notes << "\n";
  }

  std::cout << "\n";
  return 0;
}
