#include "core/arrangement.h"

namespace midisketch {

Arrangement::Arrangement(const std::vector<Section>& sections)
    : sections_(sections) {}

Tick Arrangement::barToTick(uint32_t bar) const {
  return bar * TICKS_PER_BAR;
}

std::pair<Tick, Tick> Arrangement::sectionToTickRange(
    const Section& section) const {
  Tick start = section.start_tick;
  Tick end = start + section.bars * TICKS_PER_BAR;
  return {start, end};
}

void Arrangement::iterateSections(
    std::function<void(const Section&)> callback) const {
  for (const auto& section : sections_) {
    callback(section);
  }
}

uint32_t Arrangement::totalBars() const {
  uint32_t total = 0;
  for (const auto& s : sections_) {
    total += s.bars;
  }
  return total;
}

Tick Arrangement::totalTicks() const {
  if (sections_.empty()) return 0;
  const auto& last = sections_.back();
  return last.start_tick + last.bars * TICKS_PER_BAR;
}

const Section* Arrangement::sectionAtBar(uint32_t bar) const {
  for (const auto& section : sections_) {
    if (bar >= section.startBar && bar < section.startBar + section.bars) {
      return &section;
    }
  }
  return nullptr;
}

}  // namespace midisketch
