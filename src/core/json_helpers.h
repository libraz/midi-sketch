/**
 * @file json_helpers.h
 * @brief JSON string escaping utilities.
 */

#pragma once

#include <cstdint>
#include <map>
#include <sstream>
#include <string>

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

  /**
   * @brief Get an integer value.
   * @param key The key to look up.
   * @param default_val Default value if key not found.
   * @return The integer value.
   */
  int getInt(const std::string& key, int default_val = 0) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    try {
      return std::stoi(it->second);
    } catch (...) {
      return default_val;
    }
  }

  /**
   * @brief Get an unsigned integer value.
   * @param key The key to look up.
   * @param default_val Default value if key not found.
   * @return The unsigned integer value.
   */
  uint32_t getUint(const std::string& key, uint32_t default_val = 0) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    try {
      return static_cast<uint32_t>(std::stoul(it->second));
    } catch (...) {
      return default_val;
    }
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
    return it->second == "true";
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

 private:
  void parse() {
    size_t pos = 0;
    skipWhitespace(pos);
    if (pos >= json_.size() || json_[pos] != '{') return;
    ++pos;

    while (pos < json_.size()) {
      skipWhitespace(pos);
      if (json_[pos] == '}') break;
      if (json_[pos] == ',') {
        ++pos;
        continue;
      }

      // Parse key
      std::string key = parseString(pos);
      if (key.empty()) break;

      skipWhitespace(pos);
      if (pos >= json_.size() || json_[pos] != ':') break;
      ++pos;
      skipWhitespace(pos);

      // Parse value
      std::string value = parseValue(pos);
      values_[key] = value;
    }
  }

  void skipWhitespace(size_t& pos) const {
    while (pos < json_.size() &&
           (json_[pos] == ' ' || json_[pos] == '\t' || json_[pos] == '\n' || json_[pos] == '\r')) {
      ++pos;
    }
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

    // Number, boolean, or null
    std::string value;
    while (pos < json_.size() && json_[pos] != ',' && json_[pos] != '}' && json_[pos] != ' ' &&
           json_[pos] != '\t' && json_[pos] != '\n') {
      value += json_[pos];
      ++pos;
    }
    return value;
  }

  std::string json_;
  std::map<std::string, std::string> values_;
};

}  // namespace json
}  // namespace midisketch
