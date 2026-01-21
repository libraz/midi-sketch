/**
 * @file json_helpers_test.cpp
 * @brief Tests for JSON helpers.
 */

#include "core/json_helpers.h"

#include <gtest/gtest.h>

#include <sstream>

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
  w.beginObject().write("japanese", "日本語").endObject();
  EXPECT_EQ(oss.str(), R"({"japanese":"日本語"})");
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

}  // namespace
}  // namespace json
}  // namespace midisketch
