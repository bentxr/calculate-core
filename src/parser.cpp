#include "parser.hpp"

#include "engine.hpp"
#include "functions.hpp"
#include "numbers.hpp"

#include <algorithm>
#include <array>
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

struct Alias {
    std::string_view bytes;
    TokenKind kind;
};

constexpr std::array<Alias, 8> aliases{{
    {"\xC3\x97", TokenKind::Star},            // ×
    {"\xC3\xB7", TokenKind::Slash},           // ÷
    {"\xE2\x88\x92", TokenKind::Minus},       // −
    {"\xCF\x80", TokenKind::Pi},              // π
    {"\xE2\x88\x9A", TokenKind::SquareRoot},  // √
    {"\xE2\x88\x9B", TokenKind::CubeRoot},    // ∛
    {"\xC2\xB2", TokenKind::Squared},         // ²
    {"\xC2\xB3", TokenKind::Cubed},           // ³
}};

// Symbols that are names: Σ ∑ (sum) and Π ∏ (product).
constexpr std::array<std::string_view, 4> symbolNames{"\xCE\xA3", "\xE2\x88\x91", "\xCE\xA0", "\xE2\x88\x8F"};

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
    case ',':
    case ';': return TokenKind::Comma;
    case '!': return TokenKind::Bang;
    case '%': return TokenKind::Percent;
    default: return TokenKind::End;  // not a single-character token
    }
}

}  // namespace

namespace {

// s[begin, end) without the spaces and tabs at either end.
Span trimmed(std::string_view s, std::size_t begin, std::size_t end) {
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) --end;
    return {begin, end};
}

}  // namespace

Lexed lex(std::string_view s) {
    Lexed out;
    const auto push = [&](TokenKind kind, std::size_t begin, std::size_t end) {
        out.tokens.push_back({kind, s.substr(begin, end - begin), {begin, end}});
    };
    std::size_t i = 0;
    std::size_t stop = s.size();
    int depth = 0;  // open parentheses
    // A keyword applies to the whole expression: it records the target (and a comment) and ends the lexing.
    const auto keyword = [&](std::size_t begin, std::size_t end) {
        if (depth > 0) {
            out.error = makeError(ErrorCode::UnexpectedToken,
                                  "'" + std::string(s.substr(begin, end - begin)) + "' applies to the whole expression: write it after the last ')'",
                                  {begin, end});
            return;
        }
        out.keyword = Span{begin, end};
        const std::size_t hash = s.find('#', end);
        out.target = trimmed(s, end, hash == std::string_view::npos ? s.size() : hash);
        if (hash != std::string_view::npos) out.comment = trimmed(s, hash + 1, s.size());
        stop = begin;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '#') {
            out.comment = trimmed(s, i + 1, s.size());
            stop = i;
            break;
        }
        if (isDigit(c) || (c == '.' && i + 1 < s.size() && isDigit(s[i + 1]))) {
            const std::size_t begin = i;
            while (i < s.size() && isDigit(s[i])) ++i;
            if (i < s.size() && s[i] == '.')
                for (++i; i < s.size() && isDigit(s[i]);) ++i;
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {  // an exponent only if digits follow
                std::size_t j = i + 1;
                if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
                else if (s.substr(j, 3) == "\xE2\x88\x92") j += 3;  // −, the calculator's minus
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
            if (s.substr(begin, i - begin) == "to") {
                keyword(begin, i);
                if (out.error) return out;
                break;
            }
            push(TokenKind::Identifier, begin, i);
            continue;
        }
        if (s.substr(i, 2) == "->" || s.substr(i, 3) == "\xE2\x86\x92") {  // before '-', and before the aliases
            keyword(i, i + (c == '-' ? 2 : 3));
            if (out.error) return out;
            break;
        }
        if (s.substr(i, 2) == ":=") {
            push(TokenKind::Assign, i, i + 2);
            i += 2;
            continue;
        }
        if (const TokenKind kind = singleCharacter(c); kind != TokenKind::End) {
            if (kind == TokenKind::LeftParen) ++depth;
            if (kind == TokenKind::RightParen && depth > 0) --depth;
            push(kind, i, i + 1);
            ++i;
            continue;
        }
        const auto symbol = std::find_if(symbolNames.begin(), symbolNames.end(),
                                         [&](std::string_view bytes) { return s.substr(i, bytes.size()) == bytes; });
        if (symbol != symbolNames.end()) {
            push(TokenKind::Identifier, i, i + symbol->size());
            i += symbol->size();
            continue;
        }
        const auto alias = std::find_if(aliases.begin(), aliases.end(),
                                        [&](const Alias& a) { return s.substr(i, a.bytes.size()) == a.bytes; });
        if (alias != aliases.end()) {
            push(alias->kind, i, i + alias->bytes.size());
            i += alias->bytes.size();
            continue;
        }
        const std::size_t length = std::min(utf8Length(static_cast<unsigned char>(c)), s.size() - i);
        out.error = makeError(ErrorCode::InvalidCharacter,
                              "Unexpected character '" + std::string(s.substr(i, length)) + "'", {i, i + length});
        return out;
    }
    push(TokenKind::End, stop, stop);
    return out;
}

namespace {

enum class Statistic { None, Mean, Variance, SampleStdev, PopulationVariance, PopulationStdev };

Statistic statisticNamed(std::string_view name) {
    if (name == "mean") return Statistic::Mean;
    if (name == "var") return Statistic::Variance;
    if (name == "stdev") return Statistic::SampleStdev;
    if (name == "varp") return Statistic::PopulationVariance;
    if (name == "stdevp") return Statistic::PopulationStdev;
    return Statistic::None;
}

// Spanish names of functions, in lowercase like every other name, and the function each one stands for.
// Other spellings of functions: Spanish calculator names and common variants. They name the function
// itself, so a convention that changes what `log` means leaves `log10` alone.
constexpr std::array<std::pair<std::string_view, FunctionId>, 11> functionAliases{{
    {"sen", FunctionId::Sin}, {"arcsen", FunctionId::Asin}, {"arccos", FunctionId::Acos},
    {"arctan", FunctionId::Atan}, {"senh", FunctionId::Sinh}, {"arcsenh", FunctionId::Asinh},
    {"arccosh", FunctionId::Acosh}, {"arctanh", FunctionId::Atanh}, {"mcd", FunctionId::Gcd},
    {"mcm", FunctionId::Lcm}, {"log10", FunctionId::Log10},
}};

// The function with this name (pi and e are constants, not functions).
std::optional<FunctionId> functionNamed(std::string_view name) {
    for (const auto& [alias, id] : functionAliases)
        if (name == alias) return id;
    for (int i = 0; i < functionCount; ++i) {
        const FunctionInfo& info = functionInfo(static_cast<FunctionId>(i));
        if (!info.name.empty() && info.name == name && info.minArgs > 0) return info.id;
    }
    return std::nullopt;
}

std::string argumentCount(int n, bool atLeast) {
    return (atLeast ? "at least " : "") + std::to_string(n) + (n == 1 ? " argument" : " arguments");
}

bool startsOperand(TokenKind k) {
    return k == TokenKind::Number || k == TokenKind::Identifier || k == TokenKind::LeftParen || k == TokenKind::Pi
        || k == TokenKind::SquareRoot || k == TokenKind::CubeRoot;
}

// Binding power of a token in the infix position; 0 ends an expression.
int leftPower(TokenKind k) {
    switch (k) {
    case TokenKind::Plus:
    case TokenKind::Minus: return 10;
    case TokenKind::Star:
    case TokenKind::Slash: return 20;
    case TokenKind::Caret: return 40;
    case TokenKind::Bang:
    case TokenKind::Percent:
    case TokenKind::Squared:
    case TokenKind::Cubed: return 50;
    default: return 0;
    }
}

// A Pratt parser that appends nodes to a post-order arena. After the first error every method
// returns -1 and nothing else is parsed.
enum class Range { None, Sum, Product };

Range rangeNamed(std::string_view name) {
    if (name == "sum" || name == "\xCE\xA3" || name == "\xE2\x88\x91") return Range::Sum;          // Σ ∑
    if (name == "product" || name == "\xCE\xA0" || name == "\xE2\x88\x8F") return Range::Product;  // Π ∏
    return Range::None;
}

// Names a variable (of a sum, or one assigned) cannot take.
bool reserved(const std::string& n) {
    return n == "pi" || n == "e" || n == "Ans" || n == "M" || functionNamed(n) || statisticNamed(n) != Statistic::None
        || rangeNamed(n) != Range::None;
}

// Sums and products write out at most this many terms in one expression, nested ones included:
// each term costs a full error analysis, in the browser too.
constexpr long long maxTerms = 10000;

class Parser {
    struct TokenRange {
        std::size_t begin;  // [begin, end): token indices
        std::size_t end;
    };

public:
    Parser(std::string_view source, std::vector<Token> tokens, const Options& options, const Names& names)
        : source_(source), tokens_(std::move(tokens)), options_(options), names_(names) {}

    Parsed run() {
        Parsed out;
        // name := expression: the expression is parsed and stored, under the name.
        std::size_t start = 0;
        if (tokens_.size() > 2 && tokens_[0].kind == TokenKind::Identifier && tokens_[1].kind == TokenKind::Assign) {
            const std::string name(tokens_[0].text);
            if (reserved(name)) {
                out.error = makeError(ErrorCode::ReservedName, "'" + name + "' is a reserved name", tokens_[0].span);
                return out;
            }
            out.assigned = name;
            position_ = 2;
            start = tokens_[2].span.begin;
        }
        const int root = expression(0);
        if (!error_ && peek().kind != TokenKind::End)
            fail(ErrorCode::UnexpectedToken, "Unexpected '" + std::string(peek().text) + "'", peek().span);
        if (error_) {
            out.error = error_;
            return out;
        }
        if (root != static_cast<int>(ast_.nodes.size()) - 1) ast_.nodes.push_back(ast_.nodes[root]);  // root last
        out.ast = std::move(ast_);
        out.expanded = expandedText({start, peek().span.begin});
        out.warnings = warnings_;
        return out;
    }

    // The source in `s` with every recorded replacement applied, in source order; a replacement inside one already
    // applied is skipped (the outer one's text holds it). Trailing spaces are trimmed.
    std::string expandedText(Span s) const {
        std::string text;
        std::size_t copied = s.begin;
        for (auto it = replacements_.lower_bound(s.begin); it != replacements_.end() && it->first < s.end; ++it) {
            if (it->first < copied) continue;
            text += std::string(source_.substr(copied, it->first - copied)) + it->second.second;
            copied = it->second.first;
        }
        if (copied < s.end) text += std::string(source_.substr(copied, s.end - copied));
        while (!text.empty() && text.back() == ' ') text.pop_back();
        return text;
    }

private:
    const Token& peek() const { return tokens_[position_]; }
    const Token& next() { return tokens_[position_++]; }

    int fail(ErrorCode code, std::string message, Span span) {
        if (!error_) error_ = makeError(code, std::move(message), span);
        return -1;
    }

    int node(FunctionId id, std::vector<int> args, Span span, std::string text = {}) {
        Node n;
        n.function = id;
        n.args = std::move(args);
        n.span = span;
        n.text = std::move(text);
        ast_.nodes.push_back(std::move(n));
        return static_cast<int>(ast_.nodes.size()) - 1;
    }

    int named(int n, const std::string& written) {
        ast_.nodes[static_cast<std::size_t>(n)].written = written;
        return n;
    }

    Span spanOf(int n) const { return ast_.nodes[static_cast<std::size_t>(n)].span; }

    int expression(int minPower) {
        int left = prefix();
        while (!error_) {
            const Token& t = peek();
            if (t.kind == TokenKind::Assign) return fail(ErrorCode::UnexpectedToken, "':=' can only follow a name at the start", t.span);
            if (startsOperand(t.kind)) {
                if (position_ > 0 && tokens_[position_ - 1].kind == TokenKind::Percent) {  // 3%2: a remainder was meant
                    const Span operand = spanOf(ast_.nodes[static_cast<std::size_t>(left)].args[0]);
                    return fail(ErrorCode::MissingOperator,
                                "Missing operator after '%' (for a remainder write rem("
                                    + std::string(source_.substr(operand.begin, operand.end - operand.begin)) + ", "
                                    + std::string(t.text) + "))",
                                t.span);
                }
                return fail(ErrorCode::MissingOperator,
                            "Missing operator before '" + std::string(t.text) + "' (write 2×π, not 2π)", t.span);
            }
            const int power = leftPower(t.kind);
            if (power <= minPower) break;
            const Token op = next();
            const Span postfix{spanOf(left).begin, op.span.end};
            switch (op.kind) {
            case TokenKind::Bang: left = node(FunctionId::Factorial, {left}, postfix); continue;
            case TokenKind::Percent: left = node(FunctionId::Percent, {left}, postfix); continue;
            case TokenKind::Squared: left = node(FunctionId::Square, {left}, postfix); continue;
            case TokenKind::Cubed: left = node(FunctionId::Cube, {left}, postfix); continue;
            default: break;
            }
            const bool bareRight = peek().kind != TokenKind::LeftParen;
            const int right = expression(op.kind == TokenKind::Caret ? power - 1 : power);  // ^ is right-associative
            if (error_) return -1;
            // A percentage that is the whole right operand of + or −: under OfValue it is a percentage of the left
            // operand (x ± x·p/100); under Divide it stays p/100. Either way the stored text says which, so a later
            // change of the convention changes nothing. Parentheses around it (`(10%)`) block this reading.
            if ((op.kind == TokenKind::Plus || op.kind == TokenKind::Minus) && bareRight
                && ast_.nodes[static_cast<std::size_t>(right)].function == FunctionId::Percent) {
                const int p = ast_.nodes[static_cast<std::size_t>(right)].args[0];
                const Span percent = spanOf(right);
                if (options_.conventions.percent == Conventions::Percent::Divide) {
                    replacements_[percent.begin] = {percent.end, "(" + expandedText(percent) + ")"};
                } else {
                    replacements_[percent.begin] = {percent.end, "((" + expandedText(spanOf(left)) + ")×(" + expandedText(spanOf(p)) + "))÷100"};
                    const Span whole{spanOf(left).begin, percent.end};
                    ast_.nodes.pop_back();  // the % node is the last one made
                    const int part = node(FunctionId::Divide,
                                          {node(FunctionId::Multiply, {left, p}, whole), node(FunctionId::Literal, {}, whole, "100")}, whole);
                    left = node(op.kind == TokenKind::Plus ? FunctionId::Add : FunctionId::Subtract, {left, part}, whole);
                    continue;
                }
            }
            const FunctionId id = op.kind == TokenKind::Plus    ? FunctionId::Add
                                : op.kind == TokenKind::Minus   ? FunctionId::Subtract
                                : op.kind == TokenKind::Star    ? FunctionId::Multiply
                                : op.kind == TokenKind::Slash   ? FunctionId::Divide
                                                                : FunctionId::Power;
            left = node(id, {left, right}, {spanOf(left).begin, spanOf(right).end});
        }
        return error_ ? -1 : left;
    }

    int prefix() {
        const Token t = next();
        switch (t.kind) {
        case TokenKind::Number:
            if (!parseDecimal(t.text)) return fail(ErrorCode::InvalidNumber, "Invalid number '" + std::string(t.text) + "'", t.span);
            return node(FunctionId::Literal, {}, t.span, std::string(t.text));
        case TokenKind::Pi: return node(FunctionId::Pi, {}, t.span);
        case TokenKind::Minus:
        case TokenKind::Plus: {
            const int operand = expression(30);
            if (error_) return -1;
            if (t.kind == TokenKind::Plus) return operand;
            return node(FunctionId::Negate, {operand}, {t.span.begin, spanOf(operand).end});
        }
        case TokenKind::SquareRoot:
        case TokenKind::CubeRoot: {
            const int operand = expression(45);
            if (error_) return -1;
            return node(t.kind == TokenKind::SquareRoot ? FunctionId::Sqrt : FunctionId::Cbrt, {operand},
                        {t.span.begin, spanOf(operand).end});
        }
        case TokenKind::LeftParen: {
            const int inner = expression(0);
            if (error_) return -1;
            if (peek().kind != TokenKind::RightParen)
                return fail(ErrorCode::MissingClosingParenthesis, "Missing ')'", {t.span.begin, peek().span.begin});
            ast_.nodes[static_cast<std::size_t>(inner)].span = {t.span.begin, next().span.end};
            return inner;
        }
        case TokenKind::Identifier: return identifier(t);
        case TokenKind::End: return fail(ErrorCode::UnexpectedEnd, "The expression ends too early", t.span);
        default: return fail(ErrorCode::UnexpectedToken, "Unexpected '" + std::string(t.text) + "'", t.span);
        }
    }

    int identifier(const Token& t) {
        const std::string name(t.text);
        for (auto b = bound_.rbegin(); b != bound_.rend(); ++b)
            if (b->name == name && peek().kind != TokenKind::LeftParen) return index(b->value, t.span);
        if (peek().kind == TokenKind::LeftParen) return call(t);
        if (peek().kind == TokenKind::Assign)  // before asking whether the name exists: the := is what is out of place
            return fail(ErrorCode::UnexpectedToken, "':=' can only follow a name at the start", peek().span);
        if (const auto found = names_.find(name); found != names_.end()) return expand(t, found->second);
        if (name == "pi") return node(FunctionId::Pi, {}, t.span);
        if (name == "e") return node(FunctionId::E, {}, t.span);
        if (name == "Ans") return fail(ErrorCode::UnknownName, "There is no previous result yet", t.span);
        if (name == "M") return fail(ErrorCode::UnknownName, "The memory is empty", t.span);
        if (functionNamed(name) || name == "mod" || statisticNamed(name) != Statistic::None || rangeNamed(name) != Range::None)
            return fail(ErrorCode::UnexpectedToken, name + " needs its arguments in parentheses: " + name + "(…)", t.span);
        return fail(ErrorCode::UnknownName, "Unknown name '" + name + "'", t.span);
    }

    // A stored expression: its nodes join this tree (all with the name's span), and the expanded
    // text gets it in parentheses. Stored texts are already expanded, so they contain no names.
    int expand(const Token& t, const std::string& text) {
        const Parsed inner = parse(text, options_, {});
        if (inner.error) return fail(inner.error->code, inner.error->message, t.span);
        const int offset = static_cast<int>(ast_.nodes.size());
        for (Node n : inner.ast.nodes) {
            for (int& a : n.args) a += offset;
            n.span = t.span;
            ast_.nodes.push_back(std::move(n));
        }
        replacements_[t.span.begin] = {t.span.end, "(" + text + ")"};
        return static_cast<int>(ast_.nodes.size()) - 1;
    }

    // An index of a sum: an exact integer literal (literals carry no sign; the grammar does).
    int index(const Integer& k, Span span) {
        const int literal = node(FunctionId::Literal, {}, span, Integer(abs(k)).str());
        return k < 0 ? node(FunctionId::Negate, {literal}, span) : literal;
    }

    // The arguments of the call whose '(' is at token `open`, split at its own commas. The last
    // range ends at its ')'. Empty when the ')' is missing.
    std::vector<TokenRange> arguments(std::size_t open) const {
        std::vector<TokenRange> parts;
        int depth = 0;
        std::size_t begin = open + 1;
        for (std::size_t i = open + 1; i < tokens_.size(); ++i) {
            const TokenKind k = tokens_[i].kind;
            if (k == TokenKind::End) return {};
            if (k == TokenKind::LeftParen) ++depth;
            else if (k == TokenKind::RightParen && depth > 0) --depth;
            else if (depth == 0 && (k == TokenKind::Comma || k == TokenKind::RightParen)) {
                parts.push_back({begin, i});
                if (k == TokenKind::RightParen) return parts;
                begin = i + 1;
            }
        }
        return {};
    }

    // One argument, which must use up exactly its tokens.
    int argument(const TokenRange& r) {
        position_ = r.begin;
        const int value = expression(0);
        if (error_) return -1;
        if (position_ != r.end) return fail(ErrorCode::UnexpectedToken, "Unexpected '" + std::string(peek().text) + "'", peek().span);
        return value;
    }

    // A limit of a sum or product: an expression evaluated exactly, which must be a whole number. Its
    // nodes refer only to each other, so they are copied out, evaluated, and dropped.
    bool limit(const TokenRange& r, const std::string& name, Integer& out) {
        const std::size_t first = ast_.nodes.size();
        const int root = argument(r);
        if (root < 0) return false;
        Ast own;
        for (std::size_t i = first; i < ast_.nodes.size(); ++i) {
            Node n = ast_.nodes[i];
            for (int& a : n.args) a -= static_cast<int>(first);
            own.nodes.push_back(std::move(n));
        }
        if (root - static_cast<int>(first) != own.root()) own.nodes.push_back(own.nodes[static_cast<std::size_t>(root) - first]);
        ast_.nodes.resize(first);
        const Span span{tokens_[r.begin].span.begin, tokens_[r.end - 1].span.end};
        const std::string message = "The limits of " + name + " must be exact whole numbers";
        if (checkExact(own)) {
            fail(ErrorCode::NotAnInteger, message, span);
            return false;
        }
        const Forward<Rational> fw = forward<Rational>(own, options_.cancel);
        if (fw.error) {
            fail(fw.error->code, fw.error->message, {fw.error->begin, fw.error->end});
            return false;
        }
        if (denominator(fw.values.back()) != 1) {
            fail(ErrorCode::NotAnInteger, message, span);
            return false;
        }
        out = numerator(fw.values.back());
        return true;
    }

    // sum(f, from, to[, variable]) and product(…): f written out once per index, joined left to right.
    int range(const Token& t, Range kind) {
        const std::string name = kind == Range::Sum ? "sum" : "product";
        const std::vector<TokenRange> parts = arguments(position_);
        if (parts.empty())
            return fail(ErrorCode::MissingClosingParenthesis, "Missing ')'", {t.span.begin, tokens_.back().span.begin});
        const std::size_t close = parts.back().end;
        const Span span{t.span.begin, tokens_[close].span.end};
        if (parts.size() != 3 && parts.size() != 4)
            return fail(ErrorCode::WrongArgumentCount, name + " takes 3 or 4 arguments: " + name + "(f; from; to) or "
                                                           + name + "(f; from; to; variable)", span);
        std::string variable = "x";
        if (parts.size() == 4) {
            const TokenRange& v = parts[3];
            const Token& token = tokens_[v.begin];
            if (v.end != v.begin + 1 || token.kind != TokenKind::Identifier)
                return fail(ErrorCode::UnexpectedToken, "The 4th argument of " + name + " is its variable's name",
                            v.end > v.begin ? token.span : span);
            variable = std::string(token.text);
            if (reserved(variable))
                return fail(ErrorCode::UnexpectedToken, "'" + variable + "' cannot be the variable of " + name, token.span);
        }
        for (const Binding& b : bound_)
            if (b.name == variable)
                return fail(ErrorCode::UnexpectedToken, "'" + variable + "' is already the variable of an outer sum or product", span);
        Integer from, to;
        if (!limit(parts[1], name, from) || !limit(parts[2], name, to)) return -1;
        const Integer count = to < from ? Integer(0) : Integer(to - from + 1);
        if (count > maxTerms - terms_)
            return fail(ErrorCode::TooManyTerms, name + " is limited to " + std::to_string(maxTerms) + " terms", span);
        terms_ += count.convert_to<long long>();  // 0 <= count <= maxTerms: it fits (the Boost guard rule)
        int total = -1;
        if (count == 0) {  // nothing to add up, but the body must still be valid
            const std::size_t size = ast_.nodes.size();
            bound_.push_back({variable, from});
            const int body = argument(parts[0]);
            bound_.pop_back();
            if (body < 0) return -1;
            ast_.nodes.resize(size);
            total = node(FunctionId::Literal, {}, span, kind == Range::Sum ? "0" : "1");
            warnings_.push_back({WarningCode::EmptyRange,
                                 name + " from " + from.str() + " to " + to.str() + " has no terms, so it is " + (kind == Range::Sum ? "0" : "1"),
                                 span.begin, span.end});
        }
        for (Integer k = from; k <= to; ++k) {
            bound_.push_back({variable, k});
            const int term = argument(parts[0]);
            bound_.pop_back();
            if (term < 0) return -1;
            total = total < 0 ? term : node(kind == Range::Sum ? FunctionId::Add : FunctionId::Multiply, {total, term}, span);
        }
        position_ = close + 1;
        return total;
    }

    int call(const Token& t) {
        if (const Range r = rangeNamed(std::string(t.text)); r != Range::None) return range(t, r);
        next();  // (
        std::vector<int> args;
        if (peek().kind != TokenKind::RightParen) {
            for (;;) {
                args.push_back(expression(0));
                if (error_) return -1;
                if (peek().kind != TokenKind::Comma) break;
                next();
            }
        }
        if (peek().kind != TokenKind::RightParen)
            return fail(ErrorCode::MissingClosingParenthesis, "Missing ')'", {t.span.begin, peek().span.begin});
        const Span span{t.span.begin, next().span.end};
        const std::string name(t.text);
        const int count = static_cast<int>(args.size());
        if (const Statistic s = statisticNamed(name); s != Statistic::None) return statistic(s, name, args, span);
        const Conventions& conventions = options_.conventions;
        const bool floored = conventions.mod == Conventions::Mod::Floored;
        std::optional<FunctionId> id = name == "mod" ? (floored ? FunctionId::FloorMod : FunctionId::Rem) : functionNamed(name);
        if (!id) return fail(ErrorCode::UnknownName, "Unknown function '" + name + "'", t.span);
        // The words a convention reads are stored in their canonical spelling.
        if (name == "mod") replacements_[t.span.begin] = {t.span.end, floored ? "floormod" : "rem"};
        if (*id == FunctionId::Log10 && name == "log") {
            const bool natural = conventions.log == Conventions::Log::Natural;
            if (count == 2) id = FunctionId::LogBase;
            else {
                if (natural) id = FunctionId::Ln;
                replacements_[t.span.begin] = {t.span.end, natural ? "ln" : "log10"};
            }
        }
        const FunctionInfo& info = functionInfo(*id);
        if (count < info.minArgs || (info.maxArgs >= 0 && count > info.maxArgs)) {
            const std::string expected = name == "log" ? "1 or 2 arguments" : argumentCount(info.minArgs, info.maxArgs < 0);
            return fail(ErrorCode::WrongArgumentCount, name + " takes " + expected, span);
        }
        return withAngles(*id, std::move(args), span, name);
    }

    // Degrees and gradians become radians on the way in, and back on the way out, as explicit
    // arithmetic through pi: its error stays visible in the report.
    int withAngles(FunctionId id, std::vector<int> args, Span span, const std::string& written) {
        const bool direct = id == FunctionId::Sin || id == FunctionId::Cos || id == FunctionId::Tan;
        const bool inverse = id == FunctionId::Asin || id == FunctionId::Acos || id == FunctionId::Atan;
        if (options_.angle == AngleUnit::Radians || (!direct && !inverse)) return named(node(id, std::move(args), span), written);
        const std::string full = options_.angle == AngleUnit::Degrees ? "180" : "200";
        if (direct) {
            const int pi = node(FunctionId::Pi, {}, span);
            const int factor = node(FunctionId::Divide, {pi, node(FunctionId::Literal, {}, span, full)}, span);
            args[0] = node(FunctionId::Multiply, {args[0], factor}, span);
            return named(node(id, std::move(args), span), written);
        }
        const int radians = named(node(id, std::move(args), span), written);
        const int top = node(FunctionId::Literal, {}, span, full);
        const int factor = node(FunctionId::Divide, {top, node(FunctionId::Pi, {}, span)}, span);
        return node(FunctionId::Multiply, {radians, factor}, span);
    }

    int sum(const std::vector<int>& terms, Span span) {
        int total = terms[0];
        for (std::size_t i = 1; i < terms.size(); ++i) total = node(FunctionId::Add, {total, terms[i]}, span);
        return total;
    }

    // Statistics become arithmetic on shared nodes (a DAG), so the error engine sees every step and
    // counts each argument's error once per path.
    int statistic(Statistic s, const std::string& name, const std::vector<int>& args, Span span) {
        const int n = static_cast<int>(args.size());
        const bool sample = s == Statistic::Variance || s == Statistic::SampleStdev;
        const int minimum = sample ? 2 : 1;
        if (n < minimum) return fail(ErrorCode::WrongArgumentCount, name + " takes " + argumentCount(minimum, true), span);
        const int count = node(FunctionId::Literal, {}, span, std::to_string(n));
        const int mean = node(FunctionId::Divide, {sum(args, span), count}, span);
        if (s == Statistic::Mean) return mean;
        std::vector<int> squares;
        for (const int x : args)
            squares.push_back(node(FunctionId::Square, {node(FunctionId::Subtract, {x, mean}, span)}, span));
        const int divisor = node(FunctionId::Literal, {}, span, std::to_string(sample ? n - 1 : n));
        const int variance = node(FunctionId::Divide, {sum(squares, span), divisor}, span);
        if (s == Statistic::Variance || s == Statistic::PopulationVariance) return variance;
        return node(FunctionId::Sqrt, {variance}, span);
    }

    std::string_view source_;
    std::vector<Token> tokens_;
    const Options& options_;
    const Names& names_;
    Ast ast_;
    std::size_t position_ = 0;
    std::optional<Error> error_;
    std::map<std::size_t, std::pair<std::size_t, std::string>> replacements_;
    struct Binding {
        std::string name;
        Integer value;
    };
    std::vector<Binding> bound_;  // the variables of the sums and products being written out, innermost last
    long long terms_ = 0;         // terms written out so far
    std::vector<Warning> warnings_;  // a span's begin → (its end, its text)
};

}  // namespace

namespace {

// `to …` alone converts Ans. Its nodes point at the keyword, as a name's point at the name.
Parsed previousResult(Span keyword, const Options& options, const Names& names) {
    Parsed out;
    const auto ans = names.find("Ans");
    if (ans == names.end()) {
        out.error = makeError(ErrorCode::UnknownName, "There is no previous result yet", keyword);
        return out;
    }
    out = parse(ans->second, options, {});  // stored texts contain no names, and are canonical
    if (out.error) {
        out.error->begin = keyword.begin;
        out.error->end = keyword.end;
        return out;
    }
    for (Node& n : out.ast.nodes) n.span = keyword;
    out.expanded = "(" + ans->second + ")";
    return out;
}

}  // namespace

Parsed parse(std::string_view source, const Options& options, const Names& names) {
    Lexed lexed = lex(source);
    if (lexed.error) {
        Parsed out;
        out.error = lexed.error;
        return out;
    }
    const auto text = [&](Span s) { return std::string(source.substr(s.begin, s.end - s.begin)); };
    const std::string comment = lexed.comment ? text(*lexed.comment) : "";
    std::optional<TargetText> target;
    if (lexed.keyword) {
        if (lexed.target.begin == lexed.target.end) {
            Parsed out;
            out.error = makeError(ErrorCode::UnexpectedEnd, "Write what to convert to after '" + text(*lexed.keyword) + "'", *lexed.keyword);
            return out;
        }
        const std::string whole = text(lexed.target);
        const std::size_t space = whole.find_first_of(" \t");
        const Span rest = space == std::string::npos ? Span{lexed.target.end, lexed.target.end}
                                                     : trimmed(source, lexed.target.begin + space, lexed.target.end);
        target = TargetText{whole.substr(0, space), text(rest), lexed.target};
    } else if (lexed.comment && lexed.tokens.size() == 1) {  // only End: a note
        Parsed out;
        out.comment = comment;
        out.commentOnly = true;
        return out;
    }
    Parsed out = lexed.keyword && lexed.tokens.size() == 1 ? previousResult(*lexed.keyword, options, names)
                                                           : Parser(source, std::move(lexed.tokens), options, names).run();
    if (!out.error) {
        out.comment = comment;
        out.target = target;
    }
    return out;
}

std::optional<Error> checkExact(const Ast& ast) {
    for (const Node& n : ast.nodes) {
        const FunctionInfo& info = functionInfo(n.function);
        if (info.exact) continue;
        const std::string name = n.function == FunctionId::Pi ? "π" : nameOf(n);
        return makeError(ErrorCode::NotAvailableInExact, errorMessage(ErrorCode::NotAvailableInExact, name), n.span);
    }
    return std::nullopt;
}

}  // namespace calculate_core::detail
