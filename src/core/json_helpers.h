/**
 * @file json_helpers.h
 * @brief JSON string escaping utilities.
 */

#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>

namespace midisketch {
namespace json {

/**
 * @brief Escapes special characters in a string for JSON output.
 *
 * Handles the following escape sequences:
 * - `"` -> `\"`
 * - `\` -> `\\`
 * - newline -> `\n`
 * - carriage return -> `\r`
 * - tab -> `\t`
 *
 * @param s The input string to escape.
 * @return The escaped string safe for JSON output.
 *
 * @example
 * ```cpp
 * json::escape("hello\"world");  // returns: hello\"world
 * json::escape("line1\nline2");  // returns: line1\nline2
 * ```
 */
inline std::string escape(const std::string& s) {
  std::string result;
  result.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        result += c;
        break;
    }
  }
  return result;
}

/**
 * @brief A streaming JSON writer with optional pretty-print support.
 *
 * Provides a fluent API for building JSON output incrementally.
 * Supports nested objects and arrays with automatic comma handling.
 *
 * @example Compact JSON output:
 * ```cpp
 * std::ostringstream oss;
 * json::Writer w(oss);
 * w.beginObject()
 *     .write("name", "test")
 *     .write("count", 42)
 *     .beginArray("items")
 *         .value(1)
 *         .value(2)
 *     .endArray()
 * .endObject();
 * // Output: {"name":"test","count":42,"items":[1,2]}
 * ```
 *
 * @example Pretty-printed JSON output:
 * ```cpp
 * std::ostringstream oss;
 * json::Writer w(oss, true);  // pretty=true
 * w.beginObject()
 *     .write("name", "test")
 * .endObject();
 * // Output:
 * // {
 * //   "name": "test"
 * // }
 * ```
 */
class Writer {
 public:
  /**
   * @brief Constructs a JSON writer.
   * @param os The output stream to write JSON to.
   * @param pretty If true, output is formatted with newlines and indentation.
   * @param indent_size Number of spaces per indentation level (default: 2).
   */
  explicit Writer(std::ostream& os, bool pretty = false, int indent_size = 2)
      : os_(os), pretty_(pretty), indent_size_(indent_size) {}

  /**
   * @brief Begins a JSON object.
   * @param key Optional key name when creating a nested object within another object.
   * @return Reference to this writer for method chaining.
   *
   * @example
   * ```cpp
   * w.beginObject();                    // Top-level object
   * w.beginObject("nested");            // Nested object with key
   * ```
   */
  Writer& beginObject(const char* key = nullptr) {
    writeCommaIfNeeded();
    if (key) writeKey(key);
    os_ << "{";
    pushContext('{');
    return *this;
  }

  /**
   * @brief Ends the current JSON object.
   * @return Reference to this writer for method chaining.
   */
  Writer& endObject() {
    popContext();
    writeNewlineIndent();
    os_ << "}";
    return *this;
  }

  /**
   * @brief Begins a JSON array.
   * @param key Optional key name when creating a nested array within an object.
   * @return Reference to this writer for method chaining.
   *
   * @example
   * ```cpp
   * w.beginArray();                     // Top-level array
   * w.beginArray("items");              // Array with key inside object
   * ```
   */
  Writer& beginArray(const char* key = nullptr) {
    writeCommaIfNeeded();
    if (key) writeKey(key);
    os_ << "[";
    pushContext('[');
    return *this;
  }

  /**
   * @brief Ends the current JSON array.
   * @return Reference to this writer for method chaining.
   */
  Writer& endArray() {
    popContext();
    writeNewlineIndent();
    os_ << "]";
    return *this;
  }

  /**
   * @brief Writes a key-value pair to the current object.
   * @tparam T The value type (numeric types).
   * @param key The property key.
   * @param value The property value.
   * @return Reference to this writer for method chaining.
   *
   * @example
   * ```cpp
   * w.write("count", 42);
   * w.write("price", 19.99);
   * ```
   */
  template <typename T>
  Writer& write(const char* key, T value) {
    writeCommaIfNeeded();
    writeKey(key);
    os_ << value;
    return *this;
  }

  /// Writes float values with enough significant digits for a lossless
  /// float -> decimal -> float round trip.
  Writer& write(const char* key, float value) {
    writeCommaIfNeeded();
    writeKey(key);
    os_ << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
    return *this;
  }

  /**
   * @brief Writes a boolean key-value pair.
   * @param key The property key.
   * @param value The boolean value.
   * @return Reference to this writer for method chaining.
   */
  Writer& write(const char* key, bool value) {
    writeCommaIfNeeded();
    writeKey(key);
    os_ << (value ? "true" : "false");
    return *this;
  }

  /**
   * @brief Writes a string key-value pair.
   * @param key The property key.
   * @param value The string value (will be escaped and quoted).
   * @return Reference to this writer for method chaining.
   */
  Writer& write(const char* key, const std::string& value) {
    writeCommaIfNeeded();
    writeKey(key);
    os_ << "\"" << escape(value) << "\"";
    return *this;
  }

  /**
   * @brief Writes a string key-value pair (C-string overload).
   * @param key The property key.
   * @param value The string value (will be escaped and quoted).
   * @return Reference to this writer for method chaining.
   */
  Writer& write(const char* key, const char* value) {
    writeCommaIfNeeded();
    writeKey(key);
    os_ << "\"" << escape(value) << "\"";
    return *this;
  }

  /**
   * @brief Writes a value to the current array.
   * @tparam T The value type (numeric types).
   * @param v The value to write.
   * @return Reference to this writer for method chaining.
   *
   * @example
   * ```cpp
   * w.beginArray("numbers").value(1).value(2).value(3).endArray();
   * ```
   */
  template <typename T>
  Writer& value(T v) {
    writeCommaIfNeeded();
    writeNewlineIndent();
    os_ << v;
    return *this;
  }

  /**
   * @brief Writes a boolean value to the current array.
   * @param v The boolean value.
   * @return Reference to this writer for method chaining.
   */
  Writer& value(bool v) {
    writeCommaIfNeeded();
    writeNewlineIndent();
    os_ << (v ? "true" : "false");
    return *this;
  }

  /**
   * @brief Writes a string value to the current array.
   * @param v The string value (will be escaped and quoted).
   * @return Reference to this writer for method chaining.
   */
  Writer& value(const std::string& v) {
    writeCommaIfNeeded();
    writeNewlineIndent();
    os_ << "\"" << escape(v) << "\"";
    return *this;
  }

  /**
   * @brief Writes a string value to the current array (C-string overload).
   * @param v The string value (will be escaped and quoted).
   * @return Reference to this writer for method chaining.
   */
  Writer& value(const char* v) {
    writeCommaIfNeeded();
    writeNewlineIndent();
    os_ << "\"" << escape(v) << "\"";
    return *this;
  }

  /**
   * @brief Writes raw pre-formatted JSON as a property value.
   *
   * Use this to inject pre-built JSON without re-escaping.
   *
   * @param key The property key.
   * @param json The raw JSON string (not escaped or quoted).
   * @return Reference to this writer for method chaining.
   *
   * @example
   * ```cpp
   * w.raw("data", "[1,2,3]");  // Outputs: "data":[1,2,3]
   * ```
   */
  Writer& raw(const char* key, const std::string& json) {
    writeCommaIfNeeded();
    writeKey(key);
    os_ << json;
    return *this;
  }

  /**
   * @brief Writes a raw pre-formatted JSON value to the current array.
   * @param json The raw JSON string (not escaped or quoted).
   * @return Reference to this writer for method chaining.
   */
  Writer& rawValue(const std::string& json) {
    writeCommaIfNeeded();
    writeNewlineIndent();
    os_ << json;
    return *this;
  }

 private:
  void writeKey(const char* key) {
    writeNewlineIndent();
    os_ << "\"" << key << "\":";
    if (pretty_) os_ << " ";
  }

  void writeCommaIfNeeded() {
    if (!first_) os_ << ",";
    first_ = false;
  }

  void writeNewlineIndent() {
    if (pretty_) {
      os_ << "\n";
      for (int i = 0; i < depth_ * indent_size_; ++i) os_ << " ";
    }
  }

  void pushContext(char type) {
    (void)type;
    ++depth_;
    first_ = true;
  }

  void popContext() {
    --depth_;
    first_ = false;
  }

  std::ostream& os_;
  bool pretty_;
  int indent_size_;
  int depth_ = 0;
  bool first_ = true;
};

/**
 * @brief RAII helper for automatic JSON object scope management.
 *
 * Automatically calls beginObject() on construction and endObject() on destruction.
 *
 * @example
 * ```cpp
 * {
 *   json::ObjectScope obj(writer, "person");
 *   writer.write("name", "John");
 * }  // endObject() called automatically
 * ```
 */
class ObjectScope {
 public:
  /**
   * @brief Creates an object scope.
   * @param w The JSON writer to use.
   * @param key Optional key name for nested objects.
   */
  ObjectScope(Writer& w, const char* key = nullptr) : w_(w) { w_.beginObject(key); }

  /** @brief Destructor that closes the object. */
  ~ObjectScope() { w_.endObject(); }

  /**
   * @brief Returns the underlying writer.
   * @return Reference to the JSON writer.
   */
  Writer& writer() { return w_; }

 private:
  Writer& w_;
};

/**
 * @brief RAII helper for automatic JSON array scope management.
 *
 * Automatically calls beginArray() on construction and endArray() on destruction.
 *
 * @example
 * ```cpp
 * {
 *   json::ArrayScope arr(writer, "items");
 *   writer.value(1).value(2).value(3);
 * }  // endArray() called automatically
 * ```
 */
class ArrayScope {
 public:
  /**
   * @brief Creates an array scope.
   * @param w The JSON writer to use.
   * @param key Optional key name for arrays in objects.
   */
  ArrayScope(Writer& w, const char* key = nullptr) : w_(w) { w_.beginArray(key); }

  /** @brief Destructor that closes the array. */
  ~ArrayScope() { w_.endArray(); }

  /**
   * @brief Returns the underlying writer.
   * @return Reference to the JSON writer.
   */
  Writer& writer() { return w_; }

 private:
  Writer& w_;
};

// ============================================================================
// Simple JSON Parser for Metadata
// ============================================================================

/**
 * @brief Simple JSON parser for reading metadata.
 *
 * Supports only the subset needed for metadata parsing:
 * - Objects with string, number, and boolean values
 * - No nested objects/arrays (not needed for metadata)
 *
 * @example
 * ```cpp
 * json::Parser p(R"({"seed":12345,"key":2,"drums_enabled":true})");
 * p.getInt("seed");        // returns 12345
 * p.getInt("key");         // returns 2
 * p.getBool("drums_enabled"); // returns true
 * ```
 */
class Parser {
 public:
  /**
   * @brief Constructs a parser with JSON string.
   * @param json The JSON string to parse.
   */
  explicit Parser(const std::string& json) : json_(json) { parse(); }

  /**
   * @brief Check if a key exists.
   * @param key The key to check.
   * @return True if key exists.
   */
  bool has(const std::string& key) const { return values_.find(key) != values_.end(); }

  /// @brief Whether the input was a complete JSON object.
  bool isValid() const { return valid_ && conversion_valid_; }

  /**
   * @brief Read an integer without narrowing or accepting a partial token.
   * @return true when the key is absent or contains a value representable by T
   */
  template <typename T>
  bool readInteger(const std::string& key, T& value) const {
    static_assert(std::is_integral_v<T>, "readInteger requires an integral type");
    auto it = values_.find(key);
    if (it == values_.end()) return true;

    long long parsed = 0;
    const char* begin = it->second.data();
    const char* end = begin + it->second.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end ||
        parsed < static_cast<long long>(std::numeric_limits<T>::min()) ||
        parsed > static_cast<long long>(std::numeric_limits<T>::max())) {
      conversion_valid_ = false;
      return false;
    }

    value = static_cast<T>(parsed);
    return true;
  }

  /**
   * @brief Read a float without accepting a partial token or a non-finite value.
   *
   * Applies the same strictness as readInteger: a token the requested type cannot
   * represent (`null`, an empty string, a non-numeric string, `inf`/`nan`) leaves
   * @p value untouched and invalidates the parser. No input escapes as an exception.
   *
   * @return true when the key is absent or holds a finite decimal number
   */
  bool readFloat(const std::string& key, float& value) const {
    auto it = values_.find(key);
    if (it == values_.end()) return true;

    const char* begin = it->second.c_str();
    char* end = nullptr;
    const float parsed = std::strtof(begin, &end);
    if (end == begin || *end != '\0' || !std::isfinite(parsed)) {
      conversion_valid_ = false;
      return false;
    }

    value = parsed;
    return true;
  }

  /// @brief Mark a typed read from this object as invalid.
  void markConversionInvalid() const { conversion_valid_ = false; }

  /**
   * @brief Get an integer value.
   * @param key The key to look up.
   * @param default_val Default value if key not found.
   * @return The integer value.
   */
  int getInt(const std::string& key, int default_val = 0) const {
    int value = default_val;
    readInteger(key, value);
    return value;
  }

  /**
   * @brief Get an unsigned integer value.
   * @param key The key to look up.
   * @param default_val Default value if key not found.
   * @return The unsigned integer value.
   */
  uint32_t getUint(const std::string& key, uint32_t default_val = 0) const {
    uint32_t value = default_val;
    readInteger(key, value);
    return value;
  }

  /**
   * @brief Get a boolean value.
   * @param key The key to look up.
   * @param default_val Default value if key not found.
   * @return The boolean value.
   */
  bool getBool(const std::string& key, bool default_val = false) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    if (it->second == "true") return true;
    if (it->second == "false") return false;
    conversion_valid_ = false;
    return default_val;
  }

  /**
   * @brief Get a string value.
   * @param key The key to look up.
   * @param default_val Default value if key not found.
   * @return The string value.
   */
  std::string getString(const std::string& key, const std::string& default_val = "") const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    return it->second;
  }

  /**
   * @brief Get a float value.
   * @param key The key to look up.
   * @param default_val Default value if key not found.
   * @return The float value.
   */
  float getFloat(const std::string& key, float default_val = 0.0f) const {
    float value = default_val;
    readFloat(key, value);
    return value;
  }

  /**
   * @brief Get an int8_t value.
   * @param key The key to look up.
   * @param default_val Default value if key not found.
   * @return The int8_t value.
   */
  int8_t getInt8(const std::string& key, int8_t default_val = 0) const {
    int8_t value = default_val;
    readInteger(key, value);
    return value;
  }

  /**
   * @brief Get a nested object as a new Parser.
   * @param key The key of the nested object.
   * @return A Parser for the nested object (empty if not found).
   */
  Parser getObject(const std::string& key) const {
    auto bounds = findObjectBounds(key);
    if (bounds.first == std::string::npos) {
      return Parser("{}");
    }
    return Parser(json_.substr(bounds.first, bounds.second - bounds.first + 1));
  }

 private:
  /**
   * @brief Find the start and end positions of a nested object.
   * @param key The key of the nested object.
   * @return Pair of (start, end) positions, or (npos, npos) if not found.
   */
  std::pair<size_t, size_t> findObjectBounds(const std::string& key) const {
    // Find the key - must be at depth 1 (direct child of root object)
    std::string search_key = "\"" + key + "\"";
    size_t search_start = 0;

    while (search_start < json_.size()) {
      size_t key_pos = json_.find(search_key, search_start);
      if (key_pos == std::string::npos) {
        return {std::string::npos, std::string::npos};
      }

      // Check if this key is at the right depth (direct child of root)
      int depth = 0;
      bool in_string = false;
      for (size_t i = 0; i < key_pos; ++i) {
        if (json_[i] == '"' && (i == 0 || json_[i - 1] != '\\')) {
          in_string = !in_string;
        } else if (!in_string) {
          if (json_[i] == '{' || json_[i] == '[') {
            ++depth;
          } else if (json_[i] == '}' || json_[i] == ']') {
            --depth;
          }
        }
      }

      // Key must be at depth 1 (inside root object)
      if (depth != 1) {
        search_start = key_pos + 1;
        continue;
      }

      // Find the colon after the key
      size_t colon_pos = json_.find(':', key_pos + search_key.length());
      if (colon_pos == std::string::npos) {
        return {std::string::npos, std::string::npos};
      }

      // Skip whitespace to find the opening brace
      size_t pos = colon_pos + 1;
      while (pos < json_.size() && (json_[pos] == ' ' || json_[pos] == '\t' || json_[pos] == '\n' ||
                                    json_[pos] == '\r')) {
        ++pos;
      }

      if (pos >= json_.size() || json_[pos] != '{') {
        // Value is not an object, try next occurrence
        search_start = key_pos + 1;
        continue;
      }

      size_t start = pos;
      int obj_depth = 1;
      ++pos;

      // Find matching closing brace (track both {} and [])
      while (pos < json_.size() && obj_depth > 0) {
        if (json_[pos] == '{' || json_[pos] == '[') {
          ++obj_depth;
        } else if (json_[pos] == '}' || json_[pos] == ']') {
          --obj_depth;
        } else if (json_[pos] == '"') {
          // Skip string content
          ++pos;
          while (pos < json_.size() && json_[pos] != '"') {
            if (json_[pos] == '\\' && pos + 1 < json_.size()) {
              ++pos;  // Skip escaped character
            }
            ++pos;
          }
        }
        ++pos;
      }

      if (obj_depth != 0) {
        return {std::string::npos, std::string::npos};
      }

      return {start, pos - 1};
    }

    return {std::string::npos, std::string::npos};
  }

  void parse() {
    size_t pos = 0;
    skipWhitespace(pos);
    if (pos >= json_.size() || json_[pos] != '{') return;
    ++pos;

    skipWhitespace(pos);
    if (pos < json_.size() && json_[pos] == '}') {
      ++pos;
      skipWhitespace(pos);
      valid_ = pos == json_.size();
      return;
    }

    while (pos < json_.size()) {
      skipWhitespace(pos);
      if (pos >= json_.size() || json_[pos] != '"') return;

      // Parse key
      std::string key = parseString(pos);
      if (key.empty()) break;

      skipWhitespace(pos);
      if (pos >= json_.size() || json_[pos] != ':') break;
      ++pos;
      skipWhitespace(pos);
      if (pos >= json_.size() || json_[pos] == '}' || json_[pos] == ',') return;

      // Parse value
      const bool scalar = json_[pos] != '"' && json_[pos] != '{' && json_[pos] != '[';
      std::string value = parseValue(pos);
      if (scalar && !isValidScalar(value)) return;
      values_[key] = value;

      skipWhitespace(pos);
      if (pos >= json_.size()) return;
      if (json_[pos] == '}') {
        ++pos;
        skipWhitespace(pos);
        valid_ = pos == json_.size();
        return;
      }
      if (json_[pos] != ',') return;
      ++pos;
    }
  }

  /// @brief Insignificant whitespace per RFC 8259: space, tab, LF, CR.
  ///
  /// Defined once so that skipping whitespace and ending an unquoted token agree.
  /// They did not: a CR terminated neither, so a file saved with CRLF line endings
  /// produced tokens with a trailing CR and failed to parse.
  static bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

  void skipWhitespace(size_t& pos) const {
    while (pos < json_.size() && isWhitespace(json_[pos])) {
      ++pos;
    }
  }

  static bool isValidScalar(const std::string& value) {
    if (value == "true" || value == "false" || value == "null") return true;
    if (value.empty()) return false;
    char* end = nullptr;
    const double number = std::strtod(value.c_str(), &end);
    return end != value.c_str() && *end == '\0' && std::isfinite(number);
  }

  std::string parseString(size_t& pos) {
    if (pos >= json_.size() || json_[pos] != '"') return "";
    ++pos;
    std::string result;
    while (pos < json_.size() && json_[pos] != '"') {
      if (json_[pos] == '\\' && pos + 1 < json_.size()) {
        ++pos;
        switch (json_[pos]) {
          case 'n':
            result += '\n';
            break;
          case 'r':
            result += '\r';
            break;
          case 't':
            result += '\t';
            break;
          case '"':
            result += '"';
            break;
          case '\\':
            result += '\\';
            break;
          default:
            result += json_[pos];
            break;
        }
      } else {
        result += json_[pos];
      }
      ++pos;
    }
    if (pos < json_.size()) ++pos;  // Skip closing quote
    return result;
  }

  std::string parseValue(size_t& pos) {
    skipWhitespace(pos);
    if (pos >= json_.size()) return "";

    // String value
    if (json_[pos] == '"') {
      return parseString(pos);
    }

    // Nested object - skip and store marker
    if (json_[pos] == '{') {
      skipNestedStructure(pos, '{', '}');
      return "__object__";  // Marker indicating nested object
    }

    // Array - skip and store marker
    if (json_[pos] == '[') {
      skipNestedStructure(pos, '[', ']');
      return "__array__";  // Marker indicating array
    }

    // Number, boolean, or null
    std::string value;
    while (pos < json_.size() && json_[pos] != ',' && json_[pos] != '}' &&
           !isWhitespace(json_[pos])) {
      value += json_[pos];
      ++pos;
    }
    return value;
  }

  void skipNestedStructure(size_t& pos, char open, char close) {
    if (pos >= json_.size() || json_[pos] != open) return;
    int depth = 1;
    ++pos;
    while (pos < json_.size() && depth > 0) {
      if (json_[pos] == open) {
        ++depth;
      } else if (json_[pos] == close) {
        --depth;
      } else if (json_[pos] == '"') {
        // Skip string content
        ++pos;
        while (pos < json_.size() && json_[pos] != '"') {
          if (json_[pos] == '\\' && pos + 1 < json_.size()) {
            ++pos;
          }
          ++pos;
        }
      }
      ++pos;
    }
  }

  std::string json_;
  std::map<std::string, std::string> values_;
  bool valid_ = false;
  mutable bool conversion_valid_ = true;
};

// ============================================================================
// Visitor-based serialization helpers
// ============================================================================

struct WriteVisitor {
  Writer& w;
  void operator()(const char* k, uint8_t v) { w.write(k, static_cast<int>(v)); }
  void operator()(const char* k, int8_t v) { w.write(k, static_cast<int>(v)); }
  void operator()(const char* k, uint16_t v) { w.write(k, v); }
  void operator()(const char* k, uint32_t v) { w.write(k, v); }
  void operator()(const char* k, bool v) { w.write(k, v); }
  void operator()(const char* k, float v) { w.write(k, v); }
  template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
  void operator()(const char* k, E v) {
    w.write(k, static_cast<int>(v));
  }
  template <typename T>
  void nested(const char* k, const T& obj) {
    w.beginObject(k);
    obj.writeTo(w);
    w.endObject();
  }
};

struct ReadVisitor {
  const Parser& p;
  void operator()(const char* k, uint8_t& v) { p.readInteger(k, v); }
  void operator()(const char* k, int8_t& v) { p.readInteger(k, v); }
  void operator()(const char* k, uint16_t& v) { p.readInteger(k, v); }
  void operator()(const char* k, uint32_t& v) { p.readInteger(k, v); }
  void operator()(const char* k, bool& v) { v = p.getBool(k, v); }
  void operator()(const char* k, float& v) { v = p.getFloat(k, v); }
  template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
  void operator()(const char* k, E& v) {
    using Underlying = std::underlying_type_t<E>;
    Underlying value = static_cast<Underlying>(v);
    if (p.readInteger(k, value)) {
      v = static_cast<E>(value);
    }
  }
  template <typename T>
  void nested(const char* k, T& obj) {
    if (!p.has(k)) return;
    Parser nested_parser = p.getObject(k);
    obj.readFrom(nested_parser);
    if (!nested_parser.isValid()) {
      p.markConversionInvalid();
    }
  }
};

/// @brief Records which of a config's fields the parsed JSON actually contained.
///
/// Bit N of @ref mask corresponds to the Nth field a config's visitFields declares,
/// so the presence mask is derived from that one field list rather than restating it.
struct PresenceVisitor {
  const Parser& p;
  uint32_t mask = 0;
  uint32_t index = 0;

  template <typename T>
  void operator()(const char* k, T&) {
    if (p.has(k)) mask |= (1u << index);
    ++index;
  }
  template <typename T>
  void nested(const char* k, T&) {
    if (p.has(k)) mask |= (1u << index);
    ++index;
  }
};

/// @brief Counts the fields a config's visitFields declares.
struct CountVisitor {
  uint32_t count = 0;

  template <typename T>
  constexpr void operator()(const char*, T&) {
    ++count;
  }
  template <typename T>
  constexpr void nested(const char*, T&) {
    ++count;
  }
};

/// @brief Number of fields in @p T's field list, usable in a constant expression.
template <typename T>
constexpr uint32_t fieldCount() {
  CountVisitor v;
  T config{};
  T::visitFields(config, v);
  return v.count;
}

/// @brief Presence mask with a bit set for every field @p T declares.
template <typename T>
constexpr uint32_t allFieldsMask() {
  return fieldCount<T>() >= 32 ? ~0u : (1u << fieldCount<T>()) - 1u;
}

}  // namespace json
}  // namespace midisketch
