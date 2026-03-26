#include "json.h"

#include <cstdlib>
#include <cstring>

namespace G {
namespace {

bool IsWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

class Parser {
 public:
  Parser(std::string_view input, Allocator* allocator)
      : input_(input), allocator_(allocator) {}

  ErrorOr<JsonValue*> Parse() {
    SkipWhitespace();
    auto* value = TRY(ParseValue());
    SkipWhitespace();
    if (!Done()) return Error::Message("Trailing content after JSON value");
    return value;
  }

 private:
  bool Done() const { return pos_ >= input_.size(); }
  char Cur() const { return pos_ < input_.size() ? input_[pos_] : '\0'; }

  void SkipWhitespace() {
    while (pos_ < input_.size() && IsWhitespace(input_[pos_])) pos_++;
  }

  bool Consume(char c) {
    if (Cur() == c) {
      pos_++;
      return true;
    }
    return false;
  }

  bool Match(std::string_view s) {
    if (pos_ + s.size() > input_.size()) return false;
    if (input_.substr(pos_, s.size()) != s) return false;
    pos_ += s.size();
    return true;
  }

  ErrorOr<JsonValue*> ParseValue() {
    if (Done()) return Error::Message("Unexpected end of input");
    char c = Cur();
    if (c == '"') return ParseString();
    if (c == '{') return ParseObject();
    if (c == '[') return ParseArray();
    if (c == 't' || c == 'f') return ParseBool();
    if (c == 'n') return ParseNull();
    if (c == '-' || IsDigit(c)) return ParseNumber();
    return Error::Message("Unexpected character in JSON");
  }

  ErrorOr<JsonValue*> ParseNull() {
    if (!Match("null")) return Error::Message("Invalid token");
    return allocator_->New<JsonValue>();
  }

  ErrorOr<JsonValue*> ParseBool() {
    auto* v = allocator_->New<JsonValue>();
    v->type = JsonValue::kBool;
    if (Match("true")) {
      v->bool_val = true;
    } else if (Match("false")) {
      v->bool_val = false;
    } else {
      return Error::Message("Invalid token");
    }
    return v;
  }

  ErrorOr<JsonValue*> ParseNumber() {
    size_t start = pos_;
    if (Cur() == '-') pos_++;
    if (!IsDigit(Cur())) return Error::Message("Expected digit");
    // Integer part.
    if (Cur() == '0') {
      pos_++;
    } else {
      while (!Done() && IsDigit(Cur())) pos_++;
    }
    bool is_float = false;
    // Fractional part.
    if (Cur() == '.') {
      is_float = true;
      pos_++;
      if (!IsDigit(Cur())) return Error::Message("Expected digit after '.'");
      while (!Done() && IsDigit(Cur())) pos_++;
    }
    // Exponent.
    if (Cur() == 'e' || Cur() == 'E') {
      is_float = true;
      pos_++;
      if (Cur() == '+' || Cur() == '-') pos_++;
      if (!IsDigit(Cur())) return Error::Message("Expected digit in exponent");
      while (!Done() && IsDigit(Cur())) pos_++;
    }
    // Null-terminate a copy for strtod/strtoll.
    size_t len = pos_ - start;
    char* buf = static_cast<char*>(allocator_->Alloc(len + 1, 1));
    std::memcpy(buf, input_.data() + start, len);
    buf[len] = '\0';

    auto* v = allocator_->New<JsonValue>();
    v->type = JsonValue::kNumber;
    if (is_float) {
      v->number_val = std::strtod(buf, nullptr);
    } else {
      v->number_val = static_cast<double>(std::strtoll(buf, nullptr, 10));
    }
    return v;
  }

  ErrorOr<JsonValue*> ParseString() {
    auto str = TRY(ParseStringRaw());
    auto* v = allocator_->New<JsonValue>();
    v->type = JsonValue::kString;
    v->string_val = str;
    return v;
  }

  ErrorOr<std::string_view> ParseStringRaw() {
    if (!Consume('"')) return Error::Message("Expected '\"'");
    size_t start = pos_;
    bool has_escapes = false;
    // Scan to find end of string and check for escapes.
    while (!Done() && Cur() != '"') {
      if (Cur() == '\\') {
        has_escapes = true;
        pos_++;
        if (Done()) return Error::Message("Unexpected end of string");
      }
      pos_++;
    }
    if (Done()) return Error::Message("Unterminated string");
    size_t end = pos_;
    pos_++;  // Skip closing quote.
    if (!has_escapes) {
      return input_.substr(start, end - start);
    }
    return Unescape(input_.substr(start, end - start));
  }

  ErrorOr<std::string_view> Unescape(std::string_view raw) {
    // Allocate worst case (same size as raw, escapes only shrink).
    char* buf = static_cast<char*>(allocator_->Alloc(raw.size() + 1, 1));
    size_t out = 0;
    for (size_t i = 0; i < raw.size(); i++) {
      if (raw[i] != '\\') {
        buf[out++] = raw[i];
        continue;
      }
      i++;
      if (i >= raw.size()) return Error::Message("Bad escape");
      switch (raw[i]) {
        case '"':
          buf[out++] = '"';
          break;
        case '\\':
          buf[out++] = '\\';
          break;
        case '/':
          buf[out++] = '/';
          break;
        case 'b':
          buf[out++] = '\b';
          break;
        case 'f':
          buf[out++] = '\f';
          break;
        case 'n':
          buf[out++] = '\n';
          break;
        case 'r':
          buf[out++] = '\r';
          break;
        case 't':
          buf[out++] = '\t';
          break;
        case 'u': {
          if (i + 4 >= raw.size()) return Error::Message("Bad \\u escape");
          int cp = 0;
          for (int j = 0; j < 4; j++) {
            int d = HexDigit(raw[i + 1 + j]);
            if (d < 0) return Error::Message("Bad hex in \\u escape");
            cp = (cp << 4) | d;
          }
          i += 4;
          // Encode as UTF-8.
          if (cp < 0x80) {
            buf[out++] = static_cast<char>(cp);
          } else if (cp < 0x800) {
            buf[out++] = static_cast<char>(0xC0 | (cp >> 6));
            buf[out++] = static_cast<char>(0x80 | (cp & 0x3F));
          } else {
            buf[out++] = static_cast<char>(0xE0 | (cp >> 12));
            buf[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            buf[out++] = static_cast<char>(0x80 | (cp & 0x3F));
          }
          break;
        }
        default:
          return Error::Message("Invalid escape character");
      }
    }
    buf[out] = '\0';
    return std::string_view(buf, out);
  }

  ErrorOr<JsonValue*> ParseArray() {
    if (!Consume('[')) return Error::Message("Expected '['");
    auto* v = allocator_->New<JsonValue>();
    v->type = JsonValue::kArray;
    SkipWhitespace();
    if (Consume(']')) return v;

    JsonValue::Element** tail = &v->first_element;
    while (true) {
      SkipWhitespace();
      auto* elem = allocator_->New<JsonValue::Element>();
      elem->value = TRY(ParseValue());
      *tail = elem;
      tail = &elem->next;
      SkipWhitespace();
      if (Consume(']')) break;
      if (!Consume(',')) return Error::Message("Expected ',' or ']'");
    }
    return v;
  }

  ErrorOr<JsonValue*> ParseObject() {
    if (!Consume('{')) return Error::Message("Expected '{'");
    auto* v = allocator_->New<JsonValue>();
    v->type = JsonValue::kObject;
    SkipWhitespace();
    if (Consume('}')) return v;

    JsonValue::Member** tail = &v->first_member;
    while (true) {
      SkipWhitespace();
      auto* member = allocator_->New<JsonValue::Member>();
      member->key = TRY(ParseStringRaw());
      SkipWhitespace();
      if (!Consume(':')) return Error::Message("Expected ':'");
      SkipWhitespace();
      member->value = TRY(ParseValue());
      *tail = member;
      tail = &member->next;
      SkipWhitespace();
      if (Consume('}')) break;
      if (!Consume(',')) return Error::Message("Expected ',' or '}'");
    }
    return v;
  }

  std::string_view input_;
  size_t pos_ = 0;
  Allocator* allocator_;
};

JsonValue kNullSentinel;

}  // namespace

bool JsonValue::GetBool() const { return bool_val; }

double JsonValue::GetNumber() const { return number_val; }

long long JsonValue::GetLong() const {
  return static_cast<long long>(number_val);
}

std::string_view JsonValue::GetString() const { return string_val; }

const JsonValue& JsonValue::operator[](std::string_view key) const {
  for (auto* m = first_member; m; m = m->next) {
    if (m->key == key) return *m->value;
  }
  return kNullSentinel;
}

ErrorOr<JsonValue*> ParseJson(std::string_view input, Allocator* allocator) {
  Parser parser(input, allocator);
  return parser.Parse();
}

}  // namespace G
