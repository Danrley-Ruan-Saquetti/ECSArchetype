#include <optional>
#include <string>
#include <utility>

#include "oly/schema/hash.hpp"
#include "oly/schema/layout.hpp"
#include "oly/schema/schema.hpp"
#include "validation.hpp"

namespace oly::schema {
  namespace {
    enum class TokenKind { Word,
                           LeftBrace,
                           RightBrace,
                           Colon,
                           Comma,
                           End };

    struct Token {
      TokenKind kind = TokenKind::End;
      std::string text{};
      SourceLocation location{};
    };

    std::string describe(const Token& token) {
      return token.kind == TokenKind::End ? "end of file" : "\"" + token.text + "\"";
    }

    bool isWordCharacter(char character) {
      return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_';
    }

    class Lexer {
     private:
      std::string_view source_;
      std::string_view fileName_;
      std::size_t position_ = 0;
      SourceLocation location_{};

     public:
      Lexer(std::string_view source, std::string_view fileName)
        : source_(source),
          fileName_(fileName) {}

      Token next() {
        skipTrivia();
        const SourceLocation start = location_;
        if (position_ >= source_.size()) {
          return {TokenKind::End, {}, start};
        }

        const char character = source_[position_];
        if (isWordCharacter(character)) {
          std::string word;
          while (position_ < source_.size() && isWordCharacter(source_[position_])) {
            word += source_[position_];
            advance();
          }

          return {TokenKind::Word, std::move(word), start};
        }

        const std::optional<TokenKind> punctuation = punctuationKind(character);
        if (!punctuation) {
          throw SchemaError(fileName_, start, std::string("unexpected character '") + character + "'");
        }

        advance();
        return {*punctuation, std::string(1, character), start};
      }

     private:
      static std::optional<TokenKind> punctuationKind(char character) {
        switch (character) {
        case '{':
          return TokenKind::LeftBrace;
        case '}':
          return TokenKind::RightBrace;
        case ':':
          return TokenKind::Colon;
        case ',':
          return TokenKind::Comma;
        default:
          return std::nullopt;
        }
      }

      void advance() {
        if (source_[position_] == '\n') {
          ++location_.line;
          location_.column = 1;
        } else {
          ++location_.column;
        }

        ++position_;
      }

      void skipTrivia() {
        while (position_ < source_.size()) {
          const char character = source_[position_];
          if (character == '#') {
            while (position_ < source_.size() && source_[position_] != '\n') {
              advance();
            }
          } else if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
            advance();
          } else {
            return;
          }
        }
      }
    };

    class Parser {
     private:
      Lexer lexer_;
      std::string_view fileName_;
      Token current_;
      Schema schema_{};

     public:
      Parser(std::string_view source, std::string_view fileName)
        : lexer_(source, fileName),
          fileName_(fileName),
          current_(lexer_.next()) {
        schema_.fileName = std::string(fileName);
      }

      Schema parse() {
        while (current_.kind != TokenKind::End) {
          parseDeclaration();
        }

        return std::move(schema_);
      }

     private:
      [[noreturn]] void fail(SourceLocation location, const std::string& message) const {
        throw SchemaError(fileName_, location, message);
      }

      void advance() {
        current_ = lexer_.next();
      }

      Token take(TokenKind kind, const std::string& expectation) {
        if (current_.kind != kind) {
          fail(current_.location, expectation + ", found " + describe(current_));
        }

        Token taken = std::move(current_);
        advance();

        return taken;
      }

      void parseDeclaration() {
        if (current_.kind != TokenKind::Word) {
          fail(current_.location, "expected a declaration, found " + describe(current_));
        }

        const std::string keyword = current_.text;
        if (keyword == "component") {
          advance();
          parseTypeWithFields(TypeKind::Component, keyword);
        } else if (keyword == "event") {
          advance();
          parseTypeWithFields(TypeKind::Event, keyword);
        } else if (keyword == "tag") {
          advance();
          parseTag();
        } else if (keyword == "action") {
          advance();
          parseAction();
        } else if (keyword == "state") {
          advance();
          const Token name = take(TokenKind::Word, "expected a state name after state");
          schema_.states.push_back({name.text, name.location});
        } else {
          fail(current_.location, "unknown declaration \"" + keyword + "\"; use component, tag, event, action or state");
        }
      }

      void parseTag() {
        const Token name = take(TokenKind::Word, "expected a type name after tag");
        schema_.types.push_back({TypeKind::Tag, name.text, {}, {}, name.location});
      }

      void parseTypeWithFields(TypeKind kind, const std::string& keyword) {
        const Token name = take(TokenKind::Word, "expected a type name after " + keyword);
        take(TokenKind::LeftBrace, "expected \"{\" after " + keyword + " " + name.text);
        Type type{kind, name.text, {}, {}, name.location};
        parseFields(type);
        (kind == TypeKind::Event ? schema_.events : schema_.types).push_back(std::move(type));
      }

      void parseFields(Type& type) {
        while (current_.kind != TokenKind::RightBrace) {
          const Token name = take(TokenKind::Word, "expected a field name in " + type.name);
          take(TokenKind::Colon, "expected \":\" after field " + name.text);
          const Token typeToken = take(TokenKind::Word, "expected a type for field " + name.text);
          type.fields.push_back({name.text, scalarTypeOf(typeToken, name.text, type.name), 0, name.location});
          if (current_.kind == TokenKind::Comma) {
            advance();
          } else if (current_.kind != TokenKind::RightBrace) {
            fail(current_.location, "expected \",\" or \"}\" after field " + name.text + ", found " + describe(current_));
          }
        }

        advance();
      }

      ScalarType scalarTypeOf(const Token& token, const std::string& fieldName, const std::string& typeName) const {
        for (int index = 0; index <= static_cast<int>(ScalarType::Entity); ++index) {
          if (scalarTypeName(static_cast<ScalarType>(index)) == token.text) {
            return static_cast<ScalarType>(index);
          }
        }

        fail(token.location, "unknown type \"" + token.text + "\" for field " + fieldName + " in " + typeName + "; use f32, f64, i8, u8, i16, u16, i32, u32 or entity");
      }

      void parseAction() {
        const Token name = take(TokenKind::Word, "expected an action name after action");
        take(TokenKind::Colon, "expected \":\" after action " + name.text);
        const Token kindToken = take(TokenKind::Word, "expected an action kind for " + name.text);
        schema_.actions.push_back({name.text, actionKindOf(kindToken, name.text), name.location});
      }

      ActionKind actionKindOf(const Token& token, const std::string& actionName) const {
        for (int index = 0; index <= static_cast<int>(ActionKind::Pointer); ++index) {
          if (actionKindName(static_cast<ActionKind>(index)) == token.text) {
            return static_cast<ActionKind>(index);
          }
        }

        fail(token.location, "action " + actionName + ": unknown kind \"" + token.text + "\"; use button, axis2d or pointer");
      }
    };
  }

  Schema parseSchema(std::string_view source, std::string_view fileName) {
    Schema schema = Parser(source, fileName).parse();
    validateSchema(schema);
    for (Type& type : schema.types) {
      type.layout = computeLayout(type.fields);
    }

    for (Type& event : schema.events) {
      event.layout = computeLayout(event.fields);
    }

    schema.hash = fnv1a32(canonicalForm(schema));

    return schema;
  }
}
