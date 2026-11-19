// A minimal JSON: enough for events and control messages.
// Objects keep insertion order so files stay readable.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace apron::json {

struct Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Value {
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v;

  Value() : v(nullptr) {}
  Value(std::nullptr_t) : v(nullptr) {}
  Value(bool b) : v(b) {}
  Value(double d) : v(d) {}
  Value(int i) : v(static_cast<double>(i)) {}
  Value(long long i) : v(static_cast<double>(i)) {}
  Value(const char* s) : v(std::string(s)) {}
  Value(std::string s) : v(std::move(s)) {}
  Value(Array a) : v(std::move(a)) {}
  Value(Object o) : v(std::move(o)) {}

  bool is_null() const { return std::holds_alternative<std::nullptr_t>(v); }
  bool is_bool() const { return std::holds_alternative<bool>(v); }
  bool is_number() const { return std::holds_alternative<double>(v); }
  bool is_string() const { return std::holds_alternative<std::string>(v); }
  bool is_array() const { return std::holds_alternative<Array>(v); }
  bool is_object() const { return std::holds_alternative<Object>(v); }

  const std::string& str() const { return std::get<std::string>(v); }
  double num() const { return std::get<double>(v); }
  bool boolean() const { return std::get<bool>(v); }
  const Array& arr() const { return std::get<Array>(v); }
  const Object& obj() const { return std::get<Object>(v); }

  // Looks up a key; nullptr if this is not an object or the key is missing.
  const Value* find(std::string_view key) const;
  // The key's string, or the fallback if it is missing or not a string.
  std::string get_str(std::string_view key, std::string_view fallback = "") const;
};

std::string dump(const Value& value);
std::optional<Value> parse(std::string_view text, std::string* error = nullptr);

}  // namespace apron::json
