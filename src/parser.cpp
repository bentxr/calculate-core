#include "parser.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace calculate_core::detail {

namespace {

// Own ASCII classification: <cctype> depends on the locale and misbehaves on negative char.
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

// Bytes in the UTF-8 sequence that starts with `lead`.
std::size_t utf8Length(unsigned char lead) {
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;
}

Error makeError(ErrorCode code, std::string message, Span span) {
    return Error{code, std::move(message), span.begin, span.end};
}

TokenKind singleCharacter(char c) {
    switch (c) {
    case '+': return TokenKind::Plus;
    case '-': return TokenKind::Minus;
    case '*': return TokenKind::Star;
    case '/': return TokenKind::Slash;
    case '^': return TokenKind::Caret;
    case '(': return TokenKind::LeftParen;
    case ')': return TokenKind::RightParen;
    case ',': return TokenKind::Comma;
    case '!': return TokenKind::Bang;
    case '%': return TokenKind::Percent;
    default: return TokenKind::End;  // not a single-character token
    }
}

}  // namespace

Lexed lex(std::string_view s) {
    Lexed out;
    const auto push = [&](TokenKind kind, std::size_t begin, std::size_t end) {
        out.tokens.push_back({kind, s.substr(begin, end - begin), {begin, end}});
    };
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        if (isDigit(c) || (c == '.' && i + 1 < s.size() && isDigit(s[i + 1]))) {
            const std::size_t begin = i;
            while (i < s.size() && isDigit(s[i])) ++i;
            if (i < s.size() && s[i] == '.')
                for (++i; i < s.size() && isDigit(s[i]);) ++i;
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {  // an exponent only if digits follow
                std::size_t j = i + 1;
                if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
                if (j < s.size() && isDigit(s[j]))
                    for (i = j; i < s.size() && isDigit(s[i]);) ++i;
            }
            push(TokenKind::Number, begin, i);
            continue;
        }
        if (c == '.') {
            out.error = makeError(ErrorCode::InvalidNumber, "A number needs digits", {i, i + 1});
            return out;
        }
        if (isLetter(c)) {
            const std::size_t begin = i;
            while (i < s.size() && (isLetter(s[i]) || isDigit(s[i]))) ++i;
            push(TokenKind::Identifier, begin, i);
            continue;
        }
        if (const TokenKind kind = singleCharacter(c); kind != TokenKind::End) {
            push(kind, i, i + 1);
            ++i;
            continue;
        }
        const std::size_t length = std::min(utf8Length(static_cast<unsigned char>(c)), s.size() - i);
        out.error = makeError(ErrorCode::InvalidCharacter,
                              "Unexpected character '" + std::string(s.substr(i, length)) + "'", {i, i + length});
        return out;
    }
    push(TokenKind::End, s.size(), s.size());
    return out;
}

}  // namespace calculate_core::detail
