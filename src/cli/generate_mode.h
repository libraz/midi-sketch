/**
 * @file generate_mode.h
 * @brief Generate mode for midi-sketch CLI.
 */

#ifndef MIDISKETCH_CLI_GENERATE_MODE_H
#define MIDISKETCH_CLI_GENERATE_MODE_H

#include <string>

#include "cli/args.h"
#include "core/preset_types.h"

namespace cli {

// Run generate mode: generate new MIDI
int runGenerateMode(const ParsedArgs& args);

/// @brief Outcome of rebuilding a SongConfig from embedded MIDI metadata.
enum class MetadataRestoreStatus {
  OK,             ///< Every restored field came from a value present in the metadata
  InvalidJson,    ///< The metadata document is not a complete JSON object
  MissingConfig,  ///< The declared format promises a config block that is absent
  InvalidConfig,  ///< A field holds a value the target type cannot represent
};

/// @brief Human-readable name of a metadata restore status.
const char* metadataRestoreStatusName(MetadataRestoreStatus status);

/**
 * @brief Parse MIDI metadata JSON and create a SongConfig (used by regenerate mode too).
 *
 * Anything the metadata fails to supply is reported through @p status instead of
 * being filled in from defaults unannounced; the returned config is only
 * meaningful when the status is MetadataRestoreStatus::OK.
 *
 * @param metadata Metadata JSON extracted from a MIDI file
 * @param status Optional out-parameter receiving the restore outcome
 * @return Restored configuration
 */
midisketch::SongConfig configFromMetadata(const std::string& metadata,
                                          MetadataRestoreStatus* status = nullptr);

}  // namespace cli

#endif  // MIDISKETCH_CLI_GENERATE_MODE_H
