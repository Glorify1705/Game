#include "xml.h"

#include "stringlib.h"

namespace G {
namespace {

bool IsWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool IsNameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':';
}

class Tokenizer {
 public:
  explicit Tokenizer(std::string_view input) : input_(input) {}

  bool Done() const { return pos_ >= input_.size(); }
  size_t pos() const { return pos_; }

  void SkipWhitespace() {
    while (pos_ < input_.size() && IsWhitespace(input_[pos_])) pos_++;
  }

  bool Peek(std::string_view s) const {
    if (pos_ + s.size() > input_.size()) return false;
    return input_.substr(pos_, s.size()) == s;
  }

  bool Consume(char c) {
    if (pos_ < input_.size() && input_[pos_] == c) {
      pos_++;
      return true;
    }
    return false;
  }

  char Current() const { return pos_ < input_.size() ? input_[pos_] : '\0'; }

  std::string_view ReadName() {
    size_t start = pos_;
    while (pos_ < input_.size() && IsNameChar(input_[pos_])) pos_++;
    return input_.substr(start, pos_ - start);
  }

  ErrorOr<std::string_view> ReadQuotedValue() {
    if (!Consume('"')) return Error::Message("Expected '\"'");
    size_t start = pos_;
    while (pos_ < input_.size() && input_[pos_] != '"') pos_++;
    if (pos_ >= input_.size()) {
      return Error::Message("Unterminated attribute value");
    }
    std::string_view value = input_.substr(start, pos_ - start);
    pos_++;
    return value;
  }

  void SkipUntil(char c) {
    while (pos_ < input_.size() && input_[pos_] != c) pos_++;
  }

  void Advance(size_t n) { pos_ = std::min(pos_ + n, input_.size()); }

  std::string_view Substr(size_t start, size_t len) const {
    return input_.substr(start, len);
  }

 private:
  std::string_view input_;
  size_t pos_ = 0;
};

ErrorOr<XmlElement*> ParseElement(Tokenizer* tok, Allocator* allocator);

ErrorOr<XmlAttribute*> ParseAttributes(Tokenizer* tok, Allocator* allocator) {
  XmlAttribute* head = nullptr;
  XmlAttribute** tail = &head;
  while (true) {
    tok->SkipWhitespace();
    if (tok->Done() || tok->Current() == '>' || tok->Current() == '/') break;

    auto* attr = allocator->New<XmlAttribute>();
    attr->name = tok->ReadName();
    if (attr->name.empty()) return Error::Message("Expected attribute name");
    tok->SkipWhitespace();
    if (!tok->Consume('=')) return Error::Message("Expected '='");
    tok->SkipWhitespace();
    attr->value = TRY(tok->ReadQuotedValue());

    *tail = attr;
    tail = &attr->next;
  }
  return head;
}

struct ParseChildrenResult {
  XmlElement* children;
  std::string_view text;
};

ErrorOr<ParseChildrenResult> ParseChildren(Tokenizer* tok, Allocator* allocator,
                                           std::string_view parent_tag) {
  XmlElement* head = nullptr;
  XmlElement** tail = &head;
  std::string_view text;
  while (true) {
    tok->SkipWhitespace();
    if (tok->Done()) return Error::Message("Unexpected end of input");
    if (tok->Peek("</")) {
      // Close tag.
      tok->Advance(2);
      std::string_view close_tag = tok->ReadName();
      if (close_tag != parent_tag) {
        return Error::Message("Mismatched close tag");
      }
      tok->SkipWhitespace();
      if (!tok->Consume('>')) return Error::Message("Expected '>'");
      break;
    }
    if (tok->Current() == '<') {
      auto* child = TRY(ParseElement(tok, allocator));
      *tail = child;
      tail = &child->next_sibling;
    } else {
      // Capture text content (first text run wins).
      size_t start = tok->pos();
      tok->SkipUntil('<');
      if (text.empty()) {
        text = tok->Substr(start, tok->pos() - start);
      }
    }
  }
  return ParseChildrenResult{head, text};
}

ErrorOr<XmlElement*> ParseElement(Tokenizer* tok, Allocator* allocator) {
  if (!tok->Consume('<')) return Error::Message("Expected '<'");

  auto* element = allocator->New<XmlElement>();
  element->tag = tok->ReadName();
  if (element->tag.empty()) return Error::Message("Expected tag name");

  element->first_attribute = TRY(ParseAttributes(tok, allocator));

  // Self-closing tag.
  if (tok->Consume('/')) {
    if (!tok->Consume('>')) return Error::Message("Expected '>'");
    return element;
  }

  // Open tag with children.
  if (!tok->Consume('>')) return Error::Message("Expected '>'");
  auto result = TRY(ParseChildren(tok, allocator, element->tag));
  element->first_child = result.children;
  element->text = result.text;
  return element;
}

}  // namespace

std::string_view XmlElement::Attr(std::string_view name) const {
  for (auto* attr = first_attribute; attr; attr = attr->next) {
    if (attr->name == name) return attr->value;
  }
  return {};
}

int XmlElement::AttrInt(std::string_view name) const {
  std::string_view val = Attr(name);
  if (val.empty()) return 0;
  int sign = 1;
  size_t i = 0;
  if (val[0] == '-') {
    sign = -1;
    i = 1;
  }
  int result = 0;
  for (; i < val.size(); ++i) {
    char c = val[i];
    if (c < '0' || c > '9') break;
    result = result * 10 + (c - '0');
  }
  return result * sign;
}

float XmlElement::AttrFloat(std::string_view name) const {
  return ParseFloat(Attr(name));
}

ErrorOr<XmlElement*> ParseXml(std::string_view input, Allocator* allocator) {
  Tokenizer tok(input);
  tok.SkipWhitespace();
  // Skip XML declaration (<?xml ... ?>).
  if (tok.Peek("<?")) {
    tok.Advance(2);
    while (!tok.Done()) {
      if (tok.Peek("?>")) {
        tok.Advance(2);
        break;
      }
      tok.Advance(1);
    }
    tok.SkipWhitespace();
  }
  // Skip comment nodes (<!-- ... -->).
  while (tok.Peek("<!--")) {
    tok.Advance(4);
    while (!tok.Done()) {
      if (tok.Peek("-->")) {
        tok.Advance(3);
        break;
      }
      tok.Advance(1);
    }
    tok.SkipWhitespace();
  }
  return ParseElement(&tok, allocator);
}

}  // namespace G
