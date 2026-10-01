#include "parser.hpp"

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
            push(TokenKind::Identifier, begin, i);
            continue;
        }
        if (const TokenKind kind = singleCharacter(c); kind != TokenKind::End) {
            push(kind, i, i + 1);
            ++i;
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
    push(TokenKind::End, s.size(), s.size());
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

// Names as printed on Spanish calculator keys, and the function each one stands for.
constexpr std::array<std::pair<std::string_view, std::string_view>, 10> spanishNames{{
    {"sen", "sin"}, {"Arcsen", "asin"}, {"Arccos", "acos"}, {"Arctan", "atan"},
    {"senh", "sinh"}, {"Arcsenh", "asinh"}, {"Arccosh", "acosh"}, {"Arctanh", "atanh"},
    {"MCD", "gcd"}, {"MCM", "lcm"},
}};

// The function with this name (pi and e are constants, not functions).
std::optional<FunctionId> functionNamed(std::string_view name) {
    for (const auto& [spanish, english] : spanishNames)
        if (name == spanish) name = english;
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
class Parser {
public:
    Parser(std::string_view source, std::vector<Token> tokens, AngleUnit angle, const Names& names)
        : source_(source), tokens_(std::move(tokens)), angle_(angle), names_(names) {}

    Parsed run() {
        Parsed out;
        const int root = expression(0);
        if (!error_ && peek().kind != TokenKind::End)
            fail(ErrorCode::UnexpectedToken, "Unexpected '" + std::string(peek().text) + "'", peek().span);
        if (error_) {
            out.error = error_;
            return out;
        }
        if (root != static_cast<int>(ast_.nodes.size()) - 1) ast_.nodes.push_back(ast_.nodes[root]);  // root last
        out.ast = std::move(ast_);
        out.expanded = expanded_ + std::string(source_.substr(copied_));
        return out;
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

    Span spanOf(int n) const { return ast_.nodes[static_cast<std::size_t>(n)].span; }

    int expression(int minPower) {
        int left = prefix();
        while (!error_) {
            const Token& t = peek();
            if (startsOperand(t.kind))
                return fail(ErrorCode::MissingOperator,
                            "Missing operator before '" + std::string(t.text) + "' (write 2×π, not 2π)", t.span);
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
            const int right = expression(op.kind == TokenKind::Caret ? power - 1 : power);  // ^ is right-associative
            if (error_) return -1;
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
        if (peek().kind == TokenKind::LeftParen) return call(t);
        if (const auto found = names_.find(name); found != names_.end()) return expand(t, found->second);
        if (name == "pi") return node(FunctionId::Pi, {}, t.span);
        if (name == "e") return node(FunctionId::E, {}, t.span);
        if (name == "Ans") return fail(ErrorCode::UnknownName, "There is no previous result yet", t.span);
        if (name == "M") return fail(ErrorCode::UnknownName, "The memory is empty", t.span);
        if (functionNamed(name) || statisticNamed(name) != Statistic::None)
            return fail(ErrorCode::UnexpectedToken, name + " needs its arguments in parentheses: " + name + "(…)", t.span);
        return fail(ErrorCode::UnknownName, "Unknown name '" + name + "'", t.span);
    }

    // A stored expression: its nodes join this tree (all with the name's span), and the expanded
    // text gets it in parentheses. Stored texts are already expanded, so they contain no names.
    int expand(const Token& t, const std::string& text) {
        const Parsed inner = parse(text, angle_, {});
        if (inner.error) return fail(inner.error->code, inner.error->message, t.span);
        const int offset = static_cast<int>(ast_.nodes.size());
        for (Node n : inner.ast.nodes) {
            for (int& a : n.args) a += offset;
            n.span = t.span;
            ast_.nodes.push_back(std::move(n));
        }
        expanded_ += std::string(source_.substr(copied_, t.span.begin - copied_)) + "(" + text + ")";
        copied_ = t.span.end;
        return static_cast<int>(ast_.nodes.size()) - 1;
    }

    int call(const Token& t) {
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
        std::optional<FunctionId> id = functionNamed(name);
        if (!id) return fail(ErrorCode::UnknownName, "Unknown function '" + name + "'", t.span);
        if (*id == FunctionId::Log10 && count == 2) id = FunctionId::LogBase;
        const FunctionInfo& info = functionInfo(*id);
        if (count < info.minArgs || (info.maxArgs >= 0 && count > info.maxArgs)) {
            const std::string expected = name == "log" ? "1 or 2 arguments" : argumentCount(info.minArgs, info.maxArgs < 0);
            return fail(ErrorCode::WrongArgumentCount, name + " takes " + expected, span);
        }
        return withAngles(*id, std::move(args), span);
    }

    // Degrees and gradians become radians on the way in, and back on the way out, as explicit
    // arithmetic through pi: its error stays visible in the report.
    int withAngles(FunctionId id, std::vector<int> args, Span span) {
        const bool direct = id == FunctionId::Sin || id == FunctionId::Cos || id == FunctionId::Tan;
        const bool inverse = id == FunctionId::Asin || id == FunctionId::Acos || id == FunctionId::Atan;
        if (angle_ == AngleUnit::Radians || (!direct && !inverse)) return node(id, std::move(args), span);
        const std::string full = angle_ == AngleUnit::Degrees ? "180" : "200";
        if (direct) {
            const int pi = node(FunctionId::Pi, {}, span);
            const int factor = node(FunctionId::Divide, {pi, node(FunctionId::Literal, {}, span, full)}, span);
            args[0] = node(FunctionId::Multiply, {args[0], factor}, span);
            return node(id, std::move(args), span);
        }
        const int radians = node(id, std::move(args), span);
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
    AngleUnit angle_;
    const Names& names_;
    Ast ast_;
    std::size_t position_ = 0;
    std::optional<Error> error_;
    std::string expanded_;  // the expanded source up to `copied_`
    std::size_t copied_ = 0;
};

}  // namespace

Parsed parse(std::string_view source, AngleUnit angle, const Names& names) {
    Lexed lexed = lex(source);
    if (lexed.error) {
        Parsed out;
        out.error = lexed.error;
        return out;
    }
    return Parser(source, std::move(lexed.tokens), angle, names).run();
}

std::optional<Error> checkExact(const Ast& ast) {
    for (const Node& n : ast.nodes) {
        const FunctionInfo& info = functionInfo(n.function);
        if (info.exact) continue;
        const std::string name = n.function == FunctionId::Pi ? "π" : std::string(info.name);
        return makeError(ErrorCode::NotAvailableInExact, errorMessage(ErrorCode::NotAvailableInExact, name), n.span);
    }
    return std::nullopt;
}

}  // namespace calculate_core::detail
