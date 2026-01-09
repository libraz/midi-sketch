/**
 * @file json_helpers.h
 * @brief JSON string escaping utilities.
 */

#pragma once

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
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += c; break;
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

}  // namespace json
}  // namespace midisketch
