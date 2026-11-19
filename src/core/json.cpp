#include "core/json.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace apron::json {

const Value* Value::find(std::string_view key) const {
  if (!is_object()) return nullptr;
  for (const auto& [k, val] : obj()) {
    if (k == key) return &val;
  }
  return nullptr;
}

std::string Value::get_str(std::string_view key, std::string_view fallback) const {
  const Value* found = find(key);
  if (found && found->is_string()) return found->str();
  return std::string(fallback);
}

namespace {

void dump_string(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void dump_value(std::string& out, const Value& value) {
  if (value.is_null()) {
    out += "null";
  } else if (value.is_bool()) {
    out += value.boolean() ? "true" : "false";
  } else if (value.is_number()) {
    double d = value.num();
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
      out += std::to_string(static_cast<long long>(d));
    } else {
      char buf[32];
      std::snprintf(buf, sizeof buf, "%.17g", d);
      out += buf;
    }
  } else if (value.is_string()) {
    dump_string(out, value.str());
  } else if (value.is_array()) {
    out += '[';
    bool first = true;
    for (const auto& item : value.arr()) {
      if (!first) out += ',';
      first = false;
      dump_value(out, item);
    }
    out += ']';
  } else {
    out += '{';
    bool first = true;
    for (const auto& [k, item] : value.obj()) {
      if (!first) out += ',';
      first = false;
      dump_string(out, k);
      out += ':';
      dump_value(out, item);
    }
    out += '}';
  }
}

class Parser {
 public:
  explicit Parser(std::string_view text) : s_(text) {}

  std::optional<Value> run(std::string* error) {
    auto value = parse_value();
    skip_ws();
    if (value && pos_ != s_.size()) fail("trailing text after the value");
    if (!error_.empty()) {
      if (error) *error = error_;
      return std::nullopt;
    }
    return value;
  }

 private:
  std::string_view s_;
  size_t pos_ = 0;
  std::string error_;
  int depth_ = 0;

  void fail(const char* msg) {
    if (error_.empty()) error_ = std::string(msg) + " at position " + std::to_string(pos_);
  }
  void skip_ws() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) ++pos_;
  }
  bool consume(char c) {
    skip_ws();
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool literal(std::string_view word) {
    if (s_.substr(pos_, word.size()) == word) {
      pos_ += word.size();
      return true;
    }
    return false;
  }

  std::optional<Value> parse_value() {
    skip_ws();
    if (pos_ >= s_.size()) {
      fail("unexpected end");
      return std::nullopt;
    }
    if (++depth_ > 64) {
      fail("nesting too deep");
      return std::nullopt;
    }
    std::optional<Value> out;
    char c = s_[pos_];
    if (c == '{') out = parse_object();
    else if (c == '[') out = parse_array();
    else if (c == '"') {
      auto s = parse_string();
      if (s) out = Value(std::move(*s));
    } else if (literal("true")) out = Value(true);
    else if (literal("false")) out = Value(false);
    else if (literal("null")) out = Value(nullptr);
    else out = parse_number();
    --depth_;
    return out;
  }

  std::optional<Value> parse_number() {
    size_t start = pos_;
    if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
    while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' ||
                                s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '-' || s_[pos_] == '+'))
      ++pos_;
    // strtod instead of from_chars: older libc++ (macOS) lacks from_chars for double.
    std::string token(s_.substr(start, pos_ - start));
    char* end = nullptr;
    double d = std::strtod(token.c_str(), &end);
    if (start == pos_ || end != token.c_str() + token.size()) {
      fail("invalid number");
      return std::nullopt;
    }
    return Value(d);
  }

  static void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  std::optional<unsigned> hex4() {
    if (pos_ + 4 > s_.size()) return std::nullopt;
    unsigned cp = 0;
    auto [ptr, ec] = std::from_chars(s_.data() + pos_, s_.data() + pos_ + 4, cp, 16);
    if (ec != std::errc() || ptr != s_.data() + pos_ + 4) return std::nullopt;
    pos_ += 4;
    return cp;
  }

  std::optional<std::string> parse_string() {
    ++pos_;  // opening quote
    std::string out;
    while (pos_ < s_.size()) {
      char c = s_[pos_++];
      if (c == '"') return out;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= s_.size()) break;
      char e = s_[pos_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          auto cp = hex4();
          if (!cp) {
            fail("invalid \\u escape");
            return std::nullopt;
          }
          if (*cp >= 0xD800 && *cp <= 0xDBFF && literal("\\u")) {
            auto low = hex4();
            if (low && *low >= 0xDC00 && *low <= 0xDFFF) *cp = 0x10000 + ((*cp - 0xD800) << 10) + (*low - 0xDC00);
          }
          append_utf8(out, *cp);
          break;
        }
        default:
          fail("invalid escape");
          return std::nullopt;
      }
    }
    fail("unterminated string");
    return std::nullopt;
  }

  std::optional<Value> parse_array() {
    ++pos_;
    Array out;
    if (consume(']')) return Value(std::move(out));
    while (true) {
      auto item = parse_value();
      if (!item) return std::nullopt;
      out.push_back(std::move(*item));
      if (consume(',')) continue;
      if (consume(']')) return Value(std::move(out));
      fail("expected , or ]");
      return std::nullopt;
    }
  }

  std::optional<Value> parse_object() {
    ++pos_;
    Object out;
    if (consume('}')) return Value(std::move(out));
    while (true) {
      skip_ws();
      if (pos_ >= s_.size() || s_[pos_] != '"') {
        fail("expected a quoted key");
        return std::nullopt;
      }
      auto key = parse_string();
      if (!key) return std::nullopt;
      if (!consume(':')) {
        fail("expected :");
        return std::nullopt;
      }
      auto item = parse_value();
      if (!item) return std::nullopt;
      out.emplace_back(std::move(*key), std::move(*item));
      if (consume(',')) continue;
      if (consume('}')) return Value(std::move(out));
      fail("expected , or }");
      return std::nullopt;
    }
  }
};

}  // namespace

std::string dump(const Value& value) {
  std::string out;
  dump_value(out, value);
  return out;
}

std::optional<Value> parse(std::string_view text, std::string* error) {
  return Parser(text).run(error);
}

}  // namespace apron::json
