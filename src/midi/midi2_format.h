/**
 * @file midi2_format.h
 * @brief MIDI 2.0 file format constants.
 */

#ifndef MIDISKETCH_MIDI_MIDI2_FORMAT_H
#define MIDISKETCH_MIDI_MIDI2_FORMAT_H

#include <cstddef>

namespace midisketch {

/// @name MIDI 2.0 Container Format Constants
/// @{

/// ktmidi container header magic ("AAAAAAAAEEEEEEEE")
constexpr char kContainerMagic[] = "AAAAAAAAEEEEEEEE";
constexpr size_t kContainerMagicLen = 16;

/// SMF2 Clip header magic ("SMF2CLIP")
constexpr char kClipMagic[] = "SMF2CLIP";
constexpr size_t kClipMagicLen = 8;

/// @}

}  // namespace midisketch

#endif  // MIDISKETCH_MIDI_MIDI2_FORMAT_H
