/**
 * @file midi_validator.h
 * @brief MIDI file structure validator supporting SMF1 and SMF2 formats.
 */

#pragma once

#include "core/types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace midisketch {

// Detected MIDI file format
enum class DetectedMidiFormat {
  Unknown,
  SMF1,           // Standard MIDI File Type 0/1/2
  SMF2_Clip,      // SMF2CLIP (single clip)
  SMF2_Container, // SMF2CON1 (official container)
  SMF2_ktmidi     // AAAAAAAAEEEEEEEE (ktmidi container)
};

// Validation issue severity
enum class ValidationSeverity { Info, Warning, Error };

// Single validation issue
struct ValidationIssue {
  ValidationSeverity severity = ValidationSeverity::Info;
  std::string message;
  size_t offset = 0;  // Byte offset in file (0 if not applicable)
  int track_index = -1;  // Track index (-1 if not track-specific)
};

// Track info from validation
struct ValidatedTrack {
  int index = 0;
  std::string name;
  size_t length = 0;       // Bytes
  size_t event_count = 0;
  bool has_end_of_track = false;
};

// Validation result summary
struct ValidationSummary {
  size_t file_size = 0;
  DetectedMidiFormat format = DetectedMidiFormat::Unknown;
  uint16_t midi_type = 0;      // SMF type (0, 1, 2)
  uint16_t num_tracks = 0;
  uint16_t division = 0;
  std::string timing_type;     // "PPQN" or "SMPTE"
  uint16_t ticks_per_quarter = 0;
};

// Full validation report
struct MidiValidationReport {
  bool valid = false;
  ValidationSummary summary;
  std::vector<ValidatedTrack> tracks;
  std::vector<ValidationIssue> issues;

  // Convenience methods
  size_t errorCount() const;
  size_t warningCount() const;
  bool hasErrors() const { return errorCount() > 0; }

  // Convert to JSON string
  std::string toJson() const;

  // Print text report to string
  std::string toTextReport(const std::string& filename = "") const;
};

// MIDI file validator
class MidiValidator {
 public:
  MidiValidator() = default;

  // Validate MIDI file from path
  MidiValidationReport validate(const std::string& path) const;

  // Validate MIDI from memory buffer
  MidiValidationReport validate(const std::vector<uint8_t>& data) const;
  MidiValidationReport validate(const uint8_t* data, size_t size) const;

  // Format detection only (fast)
  static DetectedMidiFormat detectFormat(const uint8_t* data, size_t size);
  static std::string formatName(DetectedMidiFormat format);

 private:
  // Validate SMF1 structure
  bool validateSMF1(const uint8_t* data, size_t size,
                    MidiValidationReport& report) const;

  // Validate SMF2 Clip structure
  bool validateSMF2Clip(const uint8_t* data, size_t size,
                        MidiValidationReport& report) const;

  // Validate SMF2 Container (ktmidi)
  bool validateSMF2Container(const uint8_t* data, size_t size,
                             MidiValidationReport& report) const;

  // Helper: validate SMF1 header chunk
  bool validateSMF1Header(const uint8_t* data, size_t size,
                          MidiValidationReport& report) const;

  // Helper: validate SMF1 track chunk
  bool validateSMF1Track(const uint8_t* data, size_t size, int track_index,
                         MidiValidationReport& report,
                         ValidatedTrack& track_info) const;

  // Helper: read variable-length quantity
  static bool readVariableLength(const uint8_t* data, size_t& offset,
                                 size_t max_size, uint32_t& value);

  // Helper: read big-endian integers
  static uint16_t readUint16BE(const uint8_t* data);
  static uint32_t readUint32BE(const uint8_t* data);

  // Helper: add issue to report
  static void addError(MidiValidationReport& report, const std::string& msg,
                       size_t offset = 0, int track = -1);
  static void addWarning(MidiValidationReport& report, const std::string& msg,
                         size_t offset = 0, int track = -1);
  static void addInfo(MidiValidationReport& report, const std::string& msg,
                      size_t offset = 0, int track = -1);
};

}  // namespace midisketch
