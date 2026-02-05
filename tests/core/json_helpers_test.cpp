/**
 * @file json_helpers_test.cpp
 * @brief Tests for JSON helpers.
 */

#include "core/json_helpers.h"

#include <gtest/gtest.h>

#include <sstream>

#include "core/preset_types.h"

namespace midisketch {
namespace json {
namespace {

// ============================================================================
// escape() tests
// ============================================================================

TEST(JsonEscapeTest, PlainString) {
  EXPECT_EQ(escape("hello"), "hello");
  EXPECT_EQ(escape(""), "");
  EXPECT_EQ(escape("abc123"), "abc123");
}

TEST(JsonEscapeTest, QuoteCharacter) {
  EXPECT_EQ(escape("say \"hello\""), "say \\\"hello\\\"");
  EXPECT_EQ(escape("\""), "\\\"");
}

TEST(JsonEscapeTest, BackslashCharacter) {
  EXPECT_EQ(escape("path\\to\\file"), "path\\\\to\\\\file");
  EXPECT_EQ(escape("\\"), "\\\\");
}

TEST(JsonEscapeTest, ControlCharacters) {
  EXPECT_EQ(escape("line1\nline2"), "line1\\nline2");
  EXPECT_EQ(escape("col1\tcol2"), "col1\\tcol2");
  EXPECT_EQ(escape("text\r\n"), "text\\r\\n");
}

TEST(JsonEscapeTest, MixedSpecialCharacters) {
  EXPECT_EQ(escape("\"quoted\"\n\\path\\"), "\\\"quoted\\\"\\n\\\\path\\\\");
}

// ============================================================================
// Writer - compact mode tests
// ============================================================================

TEST(JsonWriterTest, EmptyObject) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject().endObject();
  EXPECT_EQ(oss.str(), "{}");
}

TEST(JsonWriterTest, EmptyArray) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginArray().endArray();
  EXPECT_EQ(oss.str(), "[]");
}

TEST(JsonWriterTest, SimpleObject) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject().write("name", "test").write("count", 42).endObject();
  EXPECT_EQ(oss.str(), R"({"name":"test","count":42})");
}

TEST(JsonWriterTest, ObjectWithAllTypes) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject()
      .write("str", "hello")
      .write("int", 123)
      .write("double", 3.14)
      .write("bool_true", true)
      .write("bool_false", false)
      .endObject();
  EXPECT_EQ(oss.str(),
            R"({"str":"hello","int":123,"double":3.14,"bool_true":true,"bool_false":false})");
}

TEST(JsonWriterTest, SimpleArray) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginArray().value(1).value(2).value(3).endArray();
  EXPECT_EQ(oss.str(), "[1,2,3]");
}

TEST(JsonWriterTest, ArrayWithMixedTypes) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginArray().value(42).value("hello").value(true).value(3.14).endArray();
  EXPECT_EQ(oss.str(), R"([42,"hello",true,3.14])");
}

TEST(JsonWriterTest, NestedObject) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject()
      .write("name", "outer")
      .beginObject("inner")
      .write("value", 100)
      .endObject()
      .endObject();
  EXPECT_EQ(oss.str(), R"({"name":"outer","inner":{"value":100}})");
}

TEST(JsonWriterTest, NestedArray) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject()
      .write("name", "test")
      .beginArray("items")
      .value(1)
      .value(2)
      .endArray()
      .endObject();
  EXPECT_EQ(oss.str(), R"({"name":"test","items":[1,2]})");
}

TEST(JsonWriterTest, DeeplyNested) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject()
      .beginObject("level1")
      .beginArray("array")
      .rawValue(R"({"nested":true})")
      .endArray()
      .endObject()
      .endObject();
  EXPECT_EQ(oss.str(), R"({"level1":{"array":[{"nested":true}]}})");
}

TEST(JsonWriterTest, RawJson) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject().raw("prebuilt", "[1,2,3]").write("after", "ok").endObject();
  EXPECT_EQ(oss.str(), R"({"prebuilt":[1,2,3],"after":"ok"})");
}

TEST(JsonWriterTest, StringEscaping) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject().write("quote", "say \"hi\"").write("newline", "line1\nline2").endObject();
  EXPECT_EQ(oss.str(), R"({"quote":"say \"hi\"","newline":"line1\nline2"})");
}

// ============================================================================
// Writer - pretty mode tests
// ============================================================================

TEST(JsonWriterPrettyTest, EmptyObject) {
  std::ostringstream oss;
  Writer w(oss, true);
  w.beginObject().endObject();
  EXPECT_EQ(oss.str(), "{\n}");
}

TEST(JsonWriterPrettyTest, SimpleObject) {
  std::ostringstream oss;
  Writer w(oss, true);
  w.beginObject().write("name", "test").write("count", 42).endObject();

  std::string expected = R"({
  "name": "test",
  "count": 42
})";
  EXPECT_EQ(oss.str(), expected);
}

TEST(JsonWriterPrettyTest, NestedObject) {
  std::ostringstream oss;
  Writer w(oss, true);
  w.beginObject()
      .write("outer", "value")
      .beginObject("nested")
      .write("inner", 123)
      .endObject()
      .endObject();

  std::string expected = R"({
  "outer": "value",
  "nested": {
    "inner": 123
  }
})";
  EXPECT_EQ(oss.str(), expected);
}

TEST(JsonWriterPrettyTest, ArrayInObject) {
  std::ostringstream oss;
  Writer w(oss, true);
  w.beginObject().beginArray("items").value(1).value(2).endArray().endObject();

  std::string expected = R"({
  "items": [
    1,
    2
  ]
})";
  EXPECT_EQ(oss.str(), expected);
}

TEST(JsonWriterPrettyTest, CustomIndent) {
  std::ostringstream oss;
  Writer w(oss, true, 4);  // 4-space indent
  w.beginObject().write("key", "value").endObject();

  std::string expected = "{\n    \"key\": \"value\"\n}";
  EXPECT_EQ(oss.str(), expected);
}

// ============================================================================
// RAII scope helpers tests
// ============================================================================

TEST(JsonScopeTest, ObjectScope) {
  std::ostringstream oss;
  Writer w(oss);
  {
    ObjectScope obj(w);
    w.write("inside", true);
  }
  EXPECT_EQ(oss.str(), R"({"inside":true})");
}

TEST(JsonScopeTest, ArrayScope) {
  std::ostringstream oss;
  Writer w(oss);
  {
    ArrayScope arr(w);
    w.value(1).value(2);
  }
  EXPECT_EQ(oss.str(), "[1,2]");
}

TEST(JsonScopeTest, NestedScopes) {
  std::ostringstream oss;
  Writer w(oss);
  {
    ObjectScope obj(w);
    w.write("name", "test");
    {
      ArrayScope arr(w, "items");
      w.value("a").value("b");
    }
  }
  EXPECT_EQ(oss.str(), R"({"name":"test","items":["a","b"]})");
}

TEST(JsonScopeTest, ScopeWriterAccess) {
  std::ostringstream oss;
  Writer w(oss);
  {
    ObjectScope obj(w);
    obj.writer().write("via_scope", 42);
  }
  EXPECT_EQ(oss.str(), R"({"via_scope":42})");
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(JsonEdgeCaseTest, EmptyString) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject().write("empty", "").endObject();
  EXPECT_EQ(oss.str(), R"({"empty":""})");
}

TEST(JsonEdgeCaseTest, LargeNumber) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject().write("big", 9999999999LL).write("negative", -12345).endObject();
  EXPECT_EQ(oss.str(), R"({"big":9999999999,"negative":-12345})");
}

TEST(JsonEdgeCaseTest, UnicodePassthrough) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject().write("unicode", "café").endObject();
  EXPECT_EQ(oss.str(), R"({"unicode":"café"})");
}

TEST(JsonEdgeCaseTest, MultipleArraysInObject) {
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject()
      .beginArray("first")
      .value(1)
      .endArray()
      .beginArray("second")
      .value(2)
      .endArray()
      .endObject();
  EXPECT_EQ(oss.str(), R"({"first":[1],"second":[2]})");
}

// ============================================================================
// Parser tests
// ============================================================================

TEST(JsonParserTest, EmptyObject) {
  Parser p("{}");
  EXPECT_FALSE(p.has("anything"));
}

TEST(JsonParserTest, SimpleValues) {
  Parser p(R"({"name":"test","count":42,"enabled":true})");
  EXPECT_TRUE(p.has("name"));
  EXPECT_TRUE(p.has("count"));
  EXPECT_TRUE(p.has("enabled"));
  EXPECT_FALSE(p.has("missing"));

  EXPECT_EQ(p.getString("name"), "test");
  EXPECT_EQ(p.getInt("count"), 42);
  EXPECT_TRUE(p.getBool("enabled"));
}

TEST(JsonParserTest, DefaultValues) {
  Parser p("{}");
  EXPECT_EQ(p.getInt("missing", 99), 99);
  EXPECT_EQ(p.getUint("missing", 123u), 123u);
  EXPECT_EQ(p.getBool("missing", true), true);
  EXPECT_EQ(p.getString("missing", "default"), "default");
}

TEST(JsonParserTest, IntegerTypes) {
  Parser p(R"({"positive":12345,"negative":-100,"zero":0})");
  EXPECT_EQ(p.getInt("positive"), 12345);
  EXPECT_EQ(p.getInt("negative"), -100);
  EXPECT_EQ(p.getInt("zero"), 0);
}

TEST(JsonParserTest, UnsignedIntegers) {
  Parser p(R"({"seed":4294967295})");
  EXPECT_EQ(p.getUint("seed"), 4294967295u);
}

TEST(JsonParserTest, BooleanValues) {
  Parser p(R"({"yes":true,"no":false})");
  EXPECT_TRUE(p.getBool("yes"));
  EXPECT_FALSE(p.getBool("no"));
}

TEST(JsonParserTest, StringWithEscapes) {
  Parser p(R"({"text":"hello\"world"})");
  EXPECT_EQ(p.getString("text"), "hello\"world");
}

TEST(JsonParserTest, Whitespace) {
  Parser p(R"({  "key"  :  "value"  ,  "num"  :  42  })");
  EXPECT_EQ(p.getString("key"), "value");
  EXPECT_EQ(p.getInt("num"), 42);
}

TEST(JsonParserTest, MetadataFormat) {
  // Test parsing actual metadata format
  std::string metadata = R"({
    "generator":"midi-sketch",
    "format_version":1,
    "seed":12345,
    "chord_id":5,
    "structure":12,
    "bpm":122,
    "key":0,
    "mood":0,
    "vocal_low":57,
    "vocal_high":79,
    "drums_enabled":true
  })";
  Parser p(metadata);

  EXPECT_EQ(p.getString("generator"), "midi-sketch");
  EXPECT_EQ(p.getInt("format_version"), 1);
  EXPECT_EQ(p.getUint("seed"), 12345u);
  EXPECT_EQ(p.getInt("chord_id"), 5);
  EXPECT_EQ(p.getInt("structure"), 12);
  EXPECT_EQ(p.getInt("bpm"), 122);
  EXPECT_EQ(p.getInt("key"), 0);
  EXPECT_EQ(p.getInt("vocal_low"), 57);
  EXPECT_EQ(p.getInt("vocal_high"), 79);
  EXPECT_TRUE(p.getBool("drums_enabled"));
}

TEST(JsonParserTest, InvalidJson) {
  // Parser should handle invalid JSON gracefully
  Parser p1("not json");
  EXPECT_FALSE(p1.has("key"));

  Parser p2("{broken");
  EXPECT_FALSE(p2.has("key"));

  Parser p3("");
  EXPECT_FALSE(p3.has("key"));
}

TEST(JsonParserTest, NumericStringConversion) {
  // String containing number should be parsed as string, not number
  Parser p(R"({"str_num":"42"})");
  EXPECT_EQ(p.getString("str_num"), "42");
  // getInt on a string "42" should still work via stoi
  EXPECT_EQ(p.getInt("str_num"), 42);
}

// ============================================================================
// Parser - float and int8 tests
// ============================================================================

TEST(JsonParserTest, FloatValues) {
  Parser p(R"({"ratio":0.75,"negative":-1.5,"whole":3.0})");
  EXPECT_FLOAT_EQ(p.getFloat("ratio"), 0.75f);
  EXPECT_FLOAT_EQ(p.getFloat("negative"), -1.5f);
  EXPECT_FLOAT_EQ(p.getFloat("whole"), 3.0f);
  EXPECT_FLOAT_EQ(p.getFloat("missing", 0.5f), 0.5f);
}

TEST(JsonParserTest, Int8Values) {
  Parser p(R"({"positive":100,"negative":-50,"zero":0})");
  EXPECT_EQ(p.getInt8("positive"), 100);
  EXPECT_EQ(p.getInt8("negative"), -50);
  EXPECT_EQ(p.getInt8("zero"), 0);
  EXPECT_EQ(p.getInt8("missing", -10), -10);
}

// ============================================================================
// Parser - nested object tests
// ============================================================================

TEST(JsonParserTest, NestedObject) {
  Parser p(R"({"outer":"value","nested":{"inner":42,"flag":true}})");
  EXPECT_EQ(p.getString("outer"), "value");
  EXPECT_TRUE(p.has("nested"));

  Parser nested = p.getObject("nested");
  EXPECT_EQ(nested.getInt("inner"), 42);
  EXPECT_TRUE(nested.getBool("flag"));
}

TEST(JsonParserTest, NestedObjectMissing) {
  Parser p(R"({"key":"value"})");
  Parser nested = p.getObject("missing");
  // Missing nested object should return empty parser
  EXPECT_FALSE(nested.has("anything"));
  EXPECT_EQ(nested.getInt("anything", 99), 99);
}

TEST(JsonParserTest, DeeplyNestedObject) {
  Parser p(R"({"level1":{"level2":{"level3":{"value":123}}}})");
  Parser l1 = p.getObject("level1");
  Parser l2 = l1.getObject("level2");
  Parser l3 = l2.getObject("level3");
  EXPECT_EQ(l3.getInt("value"), 123);
}

TEST(JsonParserTest, NestedObjectWithArray) {
  // Parser currently doesn't parse arrays, but should handle objects containing them
  // Note: The items array is skipped during parsing, but other fields should work
  Parser p(R"({"nested":{"count":3,"items":[1,2,3]}})");
  Parser nested = p.getObject("nested");
  // count comes before the array, so it should be parseable
  EXPECT_EQ(nested.getInt("count"), 3);
}

TEST(JsonParserTest, MultipleNestedObjects) {
  Parser p(R"({"first":{"a":1},"second":{"b":2},"third":{"c":3}})");
  EXPECT_EQ(p.getObject("first").getInt("a"), 1);
  EXPECT_EQ(p.getObject("second").getInt("b"), 2);
  EXPECT_EQ(p.getObject("third").getInt("c"), 3);
}

TEST(JsonParserTest, NestedObjectWithEscapedStrings) {
  Parser p(R"({"nested":{"text":"hello\"world"}})");
  Parser nested = p.getObject("nested");
  EXPECT_EQ(nested.getString("text"), "hello\"world");
}

// ============================================================================
// Struct serialization round-trip tests
// ============================================================================

TEST(JsonRoundTripTest, ArpeggioParams) {
  ArpeggioParams original;
  original.pattern = ArpeggioPattern::UpDown;
  original.speed = ArpeggioSpeed::Triplet;
  original.octave_range = 3;
  original.gate = 0.65f;
  original.sync_chord = false;
  original.base_velocity = 75;

  // Serialize
  std::ostringstream oss;
  Writer w(oss);
  w.beginObject();
  original.writeTo(w);
  w.endObject();

  // Deserialize
  ArpeggioParams restored;
  Parser p(oss.str());
  restored.readFrom(p);

  // Verify
  EXPECT_EQ(restored.pattern, original.pattern);
  EXPECT_EQ(restored.speed, original.speed);
  EXPECT_EQ(restored.octave_range, original.octave_range);
  EXPECT_FLOAT_EQ(restored.gate, original.gate);
  EXPECT_EQ(restored.sync_chord, original.sync_chord);
  EXPECT_EQ(restored.base_velocity, original.base_velocity);
}

TEST(JsonRoundTripTest, ChordExtensionParams) {
  ChordExtensionParams original;
  original.enable_sus = true;
  original.enable_7th = false;
  original.enable_9th = true;
  original.tritone_sub = true;
  original.sus_probability = 0.35f;
  original.seventh_probability = 0.45f;
  original.ninth_probability = 0.55f;
  original.tritone_sub_probability = 0.65f;

  std::ostringstream oss;
  Writer w(oss);
  w.beginObject();
  original.writeTo(w);
  w.endObject();

  ChordExtensionParams restored;
  Parser p(oss.str());
  restored.readFrom(p);

  EXPECT_EQ(restored.enable_sus, original.enable_sus);
  EXPECT_EQ(restored.enable_7th, original.enable_7th);
  EXPECT_EQ(restored.enable_9th, original.enable_9th);
  EXPECT_EQ(restored.tritone_sub, original.tritone_sub);
  EXPECT_FLOAT_EQ(restored.sus_probability, original.sus_probability);
  EXPECT_FLOAT_EQ(restored.seventh_probability, original.seventh_probability);
  EXPECT_FLOAT_EQ(restored.ninth_probability, original.ninth_probability);
  EXPECT_FLOAT_EQ(restored.tritone_sub_probability, original.tritone_sub_probability);
}

TEST(JsonRoundTripTest, MotifParams) {
  MotifParams original;
  original.length = MotifLength::Bars4;
  original.note_count = 8;
  original.register_high = true;
  original.rhythm_density = MotifRhythmDensity::Driving;
  original.motion = MotifMotion::WideLeap;
  original.repeat_scope = MotifRepeatScope::Section;
  original.octave_layering_chorus = false;
  original.velocity_fixed = false;
  original.melodic_freedom = 0.7f;
  original.response_mode = false;
  original.response_probability = 0.8f;
  original.contrary_motion = false;
  original.contrary_motion_strength = 0.3f;
  original.dynamic_register = false;
  original.register_offset = -5;

  std::ostringstream oss;
  Writer w(oss);
  w.beginObject();
  original.writeTo(w);
  w.endObject();

  MotifParams restored;
  Parser p(oss.str());
  restored.readFrom(p);

  EXPECT_EQ(restored.length, original.length);
  EXPECT_EQ(restored.note_count, original.note_count);
  EXPECT_EQ(restored.register_high, original.register_high);
  EXPECT_EQ(restored.rhythm_density, original.rhythm_density);
  EXPECT_EQ(restored.motion, original.motion);
  EXPECT_EQ(restored.repeat_scope, original.repeat_scope);
  EXPECT_EQ(restored.octave_layering_chorus, original.octave_layering_chorus);
  EXPECT_EQ(restored.velocity_fixed, original.velocity_fixed);
  EXPECT_FLOAT_EQ(restored.melodic_freedom, original.melodic_freedom);
  EXPECT_EQ(restored.response_mode, original.response_mode);
  EXPECT_FLOAT_EQ(restored.response_probability, original.response_probability);
  EXPECT_EQ(restored.contrary_motion, original.contrary_motion);
  EXPECT_FLOAT_EQ(restored.contrary_motion_strength, original.contrary_motion_strength);
  EXPECT_EQ(restored.dynamic_register, original.dynamic_register);
  EXPECT_EQ(restored.register_offset, original.register_offset);
}

TEST(JsonRoundTripTest, GeneratorParamsBasic) {
  GeneratorParams original;
  original.seed = 12345;
  original.chord_id = 5;
  original.structure = StructurePattern::Ballad;
  original.bpm = 128;
  original.key = Key::Eb;
  original.mood = Mood::IdolPop;
  original.style_preset_id = 3;
  original.blueprint_id = 2;
  original.vocal_low = 55;
  original.vocal_high = 82;
  original.drums_enabled = false;
  original.arpeggio_enabled = true;
  original.humanize = false;
  original.humanize_timing = 0.6f;
  original.humanize_velocity = 0.5f;
  original.addictive_mode = true;
  original.drive_feel = 75;

  std::ostringstream oss;
  Writer w(oss);
  w.beginObject();
  original.writeTo(w);
  w.endObject();

  GeneratorParams restored;
  Parser p(oss.str());
  restored.readFrom(p);

  EXPECT_EQ(restored.seed, original.seed);
  EXPECT_EQ(restored.chord_id, original.chord_id);
  EXPECT_EQ(restored.structure, original.structure);
  EXPECT_EQ(restored.bpm, original.bpm);
  EXPECT_EQ(restored.key, original.key);
  EXPECT_EQ(restored.mood, original.mood);
  EXPECT_EQ(restored.style_preset_id, original.style_preset_id);
  EXPECT_EQ(restored.blueprint_id, original.blueprint_id);
  EXPECT_EQ(restored.vocal_low, original.vocal_low);
  EXPECT_EQ(restored.vocal_high, original.vocal_high);
  EXPECT_EQ(restored.drums_enabled, original.drums_enabled);
  EXPECT_EQ(restored.arpeggio_enabled, original.arpeggio_enabled);
  EXPECT_EQ(restored.humanize, original.humanize);
  EXPECT_FLOAT_EQ(restored.humanize_timing, original.humanize_timing);
  EXPECT_FLOAT_EQ(restored.humanize_velocity, original.humanize_velocity);
  EXPECT_EQ(restored.addictive_mode, original.addictive_mode);
  EXPECT_EQ(restored.drive_feel, original.drive_feel);
}

TEST(JsonRoundTripTest, GeneratorParamsWithNestedStructs) {
  GeneratorParams original;
  original.seed = 99999;

  // Set nested arpeggio params
  original.arpeggio.pattern = ArpeggioPattern::Random;
  original.arpeggio.speed = ArpeggioSpeed::Eighth;
  original.arpeggio.octave_range = 1;
  original.arpeggio.gate = 0.5f;

  // Set nested chord extension params
  original.chord_extension.enable_sus = true;
  original.chord_extension.enable_9th = true;
  original.chord_extension.ninth_probability = 0.8f;

  // Set nested motif params
  original.motif.length = MotifLength::Bars1;
  original.motif.note_count = 4;
  original.motif.melodic_freedom = 0.9f;

  std::ostringstream oss;
  Writer w(oss);
  w.beginObject();
  original.writeTo(w);
  w.endObject();

  GeneratorParams restored;
  Parser p(oss.str());
  restored.readFrom(p);

  // Verify nested arpeggio
  EXPECT_EQ(restored.arpeggio.pattern, original.arpeggio.pattern);
  EXPECT_EQ(restored.arpeggio.speed, original.arpeggio.speed);
  EXPECT_EQ(restored.arpeggio.octave_range, original.arpeggio.octave_range);
  EXPECT_FLOAT_EQ(restored.arpeggio.gate, original.arpeggio.gate);

  // Verify nested chord extension
  EXPECT_EQ(restored.chord_extension.enable_sus, original.chord_extension.enable_sus);
  EXPECT_EQ(restored.chord_extension.enable_9th, original.chord_extension.enable_9th);
  EXPECT_FLOAT_EQ(restored.chord_extension.ninth_probability,
                  original.chord_extension.ninth_probability);

  // Verify nested motif
  EXPECT_EQ(restored.motif.length, original.motif.length);
  EXPECT_EQ(restored.motif.note_count, original.motif.note_count);
  EXPECT_FLOAT_EQ(restored.motif.melodic_freedom, original.motif.melodic_freedom);
}

TEST(JsonRoundTripTest, StyleMelodyParams) {
  StyleMelodyParams original;
  original.max_leap_interval = 12;
  original.allow_unison_repeat = false;
  original.phrase_end_resolution = 0.5f;
  original.tension_usage = 0.4f;
  original.note_density = 1.2f;
  original.min_note_division = 16;
  original.sixteenth_note_ratio = 0.3f;
  original.thirtysecond_note_ratio = 0.1f;
  original.syncopation_prob = 0.25f;
  original.allow_bar_crossing = true;
  original.verse_register_shift = -5;
  original.chorus_register_shift = 8;

  std::ostringstream oss;
  Writer w(oss);
  w.beginObject();
  original.writeTo(w);
  w.endObject();

  StyleMelodyParams restored;
  Parser p(oss.str());
  restored.readFrom(p);

  EXPECT_EQ(restored.max_leap_interval, original.max_leap_interval);
  EXPECT_EQ(restored.allow_unison_repeat, original.allow_unison_repeat);
  EXPECT_FLOAT_EQ(restored.phrase_end_resolution, original.phrase_end_resolution);
  EXPECT_FLOAT_EQ(restored.tension_usage, original.tension_usage);
  EXPECT_FLOAT_EQ(restored.note_density, original.note_density);
  EXPECT_EQ(restored.min_note_division, original.min_note_division);
  EXPECT_FLOAT_EQ(restored.sixteenth_note_ratio, original.sixteenth_note_ratio);
  EXPECT_FLOAT_EQ(restored.thirtysecond_note_ratio, original.thirtysecond_note_ratio);
  EXPECT_FLOAT_EQ(restored.syncopation_prob, original.syncopation_prob);
  EXPECT_EQ(restored.allow_bar_crossing, original.allow_bar_crossing);
  EXPECT_EQ(restored.verse_register_shift, original.verse_register_shift);
  EXPECT_EQ(restored.chorus_register_shift, original.chorus_register_shift);
}

TEST(JsonRoundTripTest, BackwardCompatibility) {
  // Test that missing fields get default values (backward compatibility)
  std::string old_format_json = R"({
    "seed": 42,
    "bpm": 120,
    "key": 0,
    "drums_enabled": true
  })";

  GeneratorParams restored;
  Parser p(old_format_json);
  restored.readFrom(p);

  // Specified fields
  EXPECT_EQ(restored.seed, 42u);
  EXPECT_EQ(restored.bpm, 120);
  EXPECT_EQ(restored.key, Key::C);
  EXPECT_TRUE(restored.drums_enabled);

  // Missing fields should have defaults
  EXPECT_EQ(restored.chord_id, 0);
  EXPECT_EQ(restored.vocal_low, 60);
  EXPECT_EQ(restored.vocal_high, 79);
  EXPECT_EQ(restored.humanize, false);  // default (changed from true)
  EXPECT_FLOAT_EQ(restored.humanize_timing, 0.4f);

  // Nested structures should have defaults
  EXPECT_EQ(restored.arpeggio.pattern, ArpeggioPattern::Up);
  EXPECT_EQ(restored.arpeggio.speed, ArpeggioSpeed::Sixteenth);
  EXPECT_EQ(restored.chord_extension.enable_7th, false);
}

}  // namespace
}  // namespace json
}  // namespace midisketch
