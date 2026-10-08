#include "cli.hpp"

#include <algorithm>
#include <istream>
#include <optional>
#include <ostream>
#include <string>

namespace calc {

using namespace calculate_core;

namespace {

constexpr const char* dim = "\x1b[2m";
constexpr const char* reset = "\x1b[0m";

struct TypeName {
    const char* option;
    NumberType type;
};

constexpr TypeName typeNames[] = {
    {"float", NumberType::Float},
    {"double", NumberType::Double},
    {"long-double", NumberType::LongDouble},
    {"exact", NumberType::Exact},
    {"binary128", NumberType::Binary128},
    {"binary256", NumberType::Binary256},
    {"binary512", NumberType::Binary512},
};

std::string usage() {
    return "Usage: calc [options] [expression ...]\n"
           "Evaluates each expression (or each line of standard input) and shows its error.\n"
           "\n"
           "Options:\n"
           "  --type <t>         float, double (default), long-double, exact,\n"
           "                     binary128, binary256 or binary512\n"
           "  --angle <u>        rad (default), deg or grad\n"
           "  --log 10|e          what log(x) means (default 10)\n"
           "  --mod truncated|floored   the sign of mod (default truncated)\n"
           "  --percent divide|of-value   x + p% adds p/100, or p% of x (default divide)\n"
           "  --json             one JSON object per expression\n"
           "  --color <when>     auto (default), always or never\n"
           "  --allow-uncertain  let discrete functions take arguments that carry error\n"
           "  --list-types       describe the number types of this build\n"
           "  --help, --version\n"
           "\n"
           "Lines M+, M- and MC add Ans to, subtract it from, or clear the memory M.\n"
           "An expression may end in 'to <target>' (to fraction: the exact stored value).\n"
           "'name := expression' stores an expression under a name.\n";
}

struct Settings {
    Options options;
    bool json = false;
    bool color = false;
};

const char* optionName(NumberType type) {
    for (const TypeName& t : typeNames)
        if (t.type == type) return t.option;
    return "";
}

const char* codeName(ErrorCode c) {
    switch (c) {
    case ErrorCode::InvalidCharacter: return "InvalidCharacter";
    case ErrorCode::InvalidNumber: return "InvalidNumber";
    case ErrorCode::UnexpectedToken: return "UnexpectedToken";
    case ErrorCode::UnexpectedEnd: return "UnexpectedEnd";
    case ErrorCode::MissingClosingParenthesis: return "MissingClosingParenthesis";
    case ErrorCode::MissingOperator: return "MissingOperator";
    case ErrorCode::UnknownName: return "UnknownName";
    case ErrorCode::WrongArgumentCount: return "WrongArgumentCount";
    case ErrorCode::NotAvailableInExact: return "NotAvailableInExact";
    case ErrorCode::LiteralOutOfRange: return "LiteralOutOfRange";
    case ErrorCode::DivisionByZero: return "DivisionByZero";
    case ErrorCode::DomainError: return "DomainError";
    case ErrorCode::Overflow: return "Overflow";
    case ErrorCode::IrrationalResult: return "IrrationalResult";
    case ErrorCode::ArgumentTooLarge: return "ArgumentTooLarge";
    case ErrorCode::NotAnInteger: return "NotAnInteger";
    case ErrorCode::UncertainDiscreteArgument: return "UncertainDiscreteArgument";
    case ErrorCode::ArgumentNearJump: return "ArgumentNearJump";
    case ErrorCode::UnknownTarget: return "UnknownTarget";
    case ErrorCode::TooManyTerms: return "TooManyTerms";
    case ErrorCode::ReservedName: return "ReservedName";
    case ErrorCode::ArgumentNearEdge: return "ArgumentNearEdge";
    case ErrorCode::Cancelled: return "Cancelled";
    }
    return "";
}

const char* warningName(WarningCode code) {
    switch (code) {
    case WarningCode::EmptyRange: return "EmptyRange";
    }
    return "";
}

void printJson(std::ostream& out, const std::string& input, const Result& r) {
    const auto flag = [](bool b) { return b ? "true" : "false"; };
    out << "{\"expression\":" << jsonString(input) << ",\"type\":" << jsonString(optionName(r.type));
    if (r.error) {
        out << ",\"error\":{\"code\":" << jsonString(codeName(r.error->code)) << ",\"message\":"
            << jsonString(r.error->message) << ",\"begin\":" << r.error->begin << ",\"end\":" << r.error->end << "}}\n";
        return;
    }
    if (r.commentOnly) {
        out << ",\"comment\":" << jsonString(r.comment) << "}\n";
        return;
    }
    if (r.exact) {
        const Fraction& f = *r.exact;
        out << ",\"exact\":{\"negative\":" << flag(f.negative) << ",\"numerator\":" << jsonString(f.numerator)
            << ",\"denominator\":" << jsonString(f.denominator) << ",\"hasDecimal\":" << flag(f.hasDecimal)
            << ",\"integerPart\":" << jsonString(f.integerPart) << ",\"fractionDigits\":" << jsonString(f.fractionDigits)
            << ",\"repeatingDigits\":" << jsonString(f.repeatingDigits) << "}";
    } else {
        out << ",\"value\":{\"negative\":" << flag(r.value.negative) << ",\"digits\":" << jsonString(r.value.digits)
            << ",\"exponent10\":" << r.value.exponent10 << "}";
    }
    out << ",\"trustedDigits\":" << r.trustedDigits << ",\"trustedDigitsMeasured\":" << r.trustedDigitsMeasured
        << ",\"bound\":" << jsonString(r.bound) << ",\"inputError\":" << jsonString(r.inputError)
        << ",\"roundingError\":" << jsonString(r.roundingError) << ",\"libraryError\":" << jsonString(r.libraryError)
        << ",\"measured\":" << jsonString(r.measured) << ",\"conditionNumber\":" << jsonString(r.conditionNumber)
        << ",\"measuredAvailable\":" << flag(r.measuredAvailable)
        << ",\"measurementReliable\":" << flag(r.measurementReliable) << ",\"boundComplete\":" << flag(r.boundComplete)
        << ",\"roundingOperations\":" << r.roundingOperations << ",\"expanded\":" << jsonString(r.expression);
    if (r.conversion)
        out << ",\"conversion\":{\"target\":" << jsonString(r.conversion->target) << ",\"text\":" << jsonString(r.conversion->text) << "}";
    if (!r.warnings.empty()) {
        out << ",\"warnings\":[";
        for (std::size_t i = 0; i < r.warnings.size(); ++i) {
            const Warning& w = r.warnings[i];
            out << (i ? "," : "") << "{\"code\":" << jsonString(warningName(w.code)) << ",\"message\":" << jsonString(w.message)
                << ",\"begin\":" << w.begin << ",\"end\":" << w.end << "}";
        }
        out << "]";
    }
    if (!r.comment.empty()) out << ",\"comment\":" << jsonString(r.comment);
    if (!r.assigned.empty()) out << ",\"assigned\":" << jsonString(r.assigned);
    out << "}\n";
}

void listTypes(std::ostream& out) {
    const auto pad = [](std::string text, std::size_t width) {
        if (text.size() < width) text.append(width - text.size(), ' ');
        return text;
    };
    for (const TypeInfo& t : numberTypes()) {
        std::string description = "exact rationals, no rounding";
        if (t.type != NumberType::Exact) {
            description = std::to_string(t.storageBits) + "-bit, " + std::to_string(t.precisionBits) +
                          "-bit significand, ~" + std::to_string(t.decimalDigits) + " digits";
            if (!t.note.empty()) description += ", " + t.note;
        }
        out << pad(optionName(t.type), 13) << pad(t.label, 11) << description << "\n";
    }
}

// Display columns of UTF-8 text: one per code point (bytes that are not continuation bytes).
std::size_t columns(const std::string& s, std::size_t from, std::size_t to) {
    std::size_t n = 0;
    for (std::size_t i = from; i < to && i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}

void printError(std::ostream& err, const std::string& input, const Error& e) {
    err << input << "\n"
        << std::string(columns(input, 0, e.begin), ' ')
        << std::string(std::max<std::size_t>(1, columns(input, e.begin, e.end)), '^') << " " << e.message << "\n";
}

// After the report lines: what is worth knowing about the result.
void printNotes(std::ostream& out, const Result& r) {
    for (const Warning& w : r.warnings) out << "  note: " << w.message << "\n";
}

void printHuman(std::ostream& out, const std::string& input, const Result& r, bool color) {
    out << input << "\n";
    if (r.commentOnly) return;  // a note: the line alone
    if (r.exact) {
        out << "= " << formatFraction(*r.exact) << "\n";
        if (r.conversion) out << "→ " << r.conversion->text << "\n";
        out << "  exact, no rounding error · κ " << r.conditionNumber << "\n";
        printNotes(out, r);
        return;
    }
    out << "= " << formatValue(r.value, r.trustedDigits, color) << "\n";
    if (r.conversion) out << "→ " << r.conversion->text << "\n";
    out << "  ± " << r.bound << "  input " << r.inputError << " · rounding " << r.roundingError << " · library "
        << r.libraryError;
    if (!r.boundComplete) out << "  (incomplete: an uncertain argument was accepted)";
    out << "\n  measured ";
    if (r.measuredAvailable) out << r.measured << (r.measurementReliable ? "" : " (unreliable)");
    else out << "unavailable";
    out << " · κ " << r.conditionNumber << " · ";
    if (r.trustedDigits >= static_cast<int>(r.value.digits.size())) out << "all digits trusted";
    else out << r.trustedDigits << (r.trustedDigits == 1 ? " trusted digit" : " trusted digits");
    out << "\n";
    printNotes(out, r);
}

// One expression or memory command. Returns false when it failed.
bool handle(const std::string& line, Session& session, const Settings& s, std::ostream& out, std::ostream& err) {
    if (line == "M+" || line == "M-" || line == "MC") {
        bool ok = true;
        if (line == "MC") session.memoryClear();
        else ok = line == "M+" ? session.memoryAdd() : session.memorySubtract();
        if (!ok) {
            err << "calc: " << line << " needs a previous result\n";
            return false;
        }
        if (s.json) out << "{\"memory\":" << jsonString(session.memory()) << "}\n";
        else out << (session.memory().empty() ? std::string("M cleared") : "M = " + session.memory()) << "\n";
        return true;
    }
    const Result r = session.evaluate(line, s.options);
    if (s.json) printJson(out, line, r);
    else if (r.error) printError(err, line, *r.error);
    else printHuman(out, line, r, s.color);
    return !r.error;
}

}  // namespace

std::string formatValue(const Digits& value, int trustedDigits, bool color) {
    const std::string& sig = value.digits;
    const long long n = static_cast<long long>(sig.size());
    const long long e = value.exponent10;
    const long long trusted = trustedDigits < n ? trustedDigits : n;
    const auto digit = [&](long long i) {
        std::string d;
        if (i == trusted && trusted < n) d += color ? std::string("|") + dim : "|";  // the bar is always there
        d += sig[static_cast<std::size_t>(i)];
        return d;
    };
    const std::string end = color && trusted < n ? reset : "";
    std::string text = value.negative ? "-" : "";
    if (e >= -7 && e < 21) {  // positional
        if (e < 0) {
            text += "0." + std::string(static_cast<std::size_t>(-e - 1), '0');
            for (long long i = 0; i < n; ++i) text += digit(i);
        } else {
            for (long long i = 0; i <= e; ++i) text += i < n ? digit(i) : "0";
            if (n > e + 1) {
                text += ".";
                for (long long i = e + 1; i < n; ++i) text += digit(i);
            }
        }
        return text + end;
    }
    text += digit(0);  // scientific
    if (n > 1) {
        text += ".";
        for (long long i = 1; i < n; ++i) text += digit(i);
    }
    return text + end + "e" + (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
}

std::string formatFraction(const Fraction& f) {
    const std::string sign = f.negative ? "-" : "";
    if (f.denominator == "1") return sign + f.numerator;
    std::string s = sign + f.numerator + "/" + f.denominator;
    if (f.hasDecimal) {
        s += " = " + sign + f.integerPart + "." + f.fractionDigits;
        if (!f.repeatingDigits.empty()) s += "(" + f.repeatingDigits + ")";
    }
    return s;
}

std::string jsonString(const std::string& s) {
    static const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += "\\u00";
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
            } else {
                out += c;  // UTF-8 passes through
            }
        }
    }
    return out + "\"";
}

int run(const std::vector<std::string>& args, std::istream& in, std::ostream& out, std::ostream& err,
        bool terminal) {
    Settings s;
    std::string colorWhen = "auto";
    std::vector<std::string> expressions;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const auto value = [&]() -> std::optional<std::string> {
            if (i + 1 < args.size()) return args[++i];
            err << "calc: " << a << " needs a value\n";
            return std::nullopt;
        };
        if (a == "--help") {
            out << usage();
            return 0;
        }
        if (a == "--version") {
            out << "calc " << CALCULATE_VERSION << "\n";
            return 0;
        }
        if (a == "--list-types") {
            listTypes(out);
            return 0;
        }
        if (a == "--json") {
            s.json = true;
            continue;
        }
        if (a == "--allow-uncertain") {
            s.options.allowUncertainDiscreteArguments = true;
            continue;
        }
        if (a == "--type") {
            const auto v = value();
            if (!v) return 2;
            bool found = false;
            for (const TypeName& t : typeNames)
                if (*v == t.option) {
                    s.options.type = t.type;
                    found = true;
                }
            if (!found) {
                err << "calc: unknown type '" << *v << "' (see --list-types)\n";
                return 2;
            }
            continue;
        }
        if (a == "--angle") {
            const auto v = value();
            if (!v) return 2;
            if (*v == "rad") s.options.angle = AngleUnit::Radians;
            else if (*v == "deg") s.options.angle = AngleUnit::Degrees;
            else if (*v == "grad") s.options.angle = AngleUnit::Gradians;
            else {
                err << "calc: unknown angle unit '" << *v << "' (rad, deg or grad)\n";
                return 2;
            }
            continue;
        }
        // The conventions: each word takes one of two values, the first being the default.
        if (a == "--log" || a == "--mod" || a == "--percent") {
            const auto v = value();
            if (!v) return 2;
            Conventions& c = s.options.conventions;
            if (a == "--log" && (*v == "10" || *v == "e")) c.log = *v == "e" ? Conventions::Log::Natural : Conventions::Log::Base10;
            else if (a == "--mod" && (*v == "truncated" || *v == "floored"))
                c.mod = *v == "floored" ? Conventions::Mod::Floored : Conventions::Mod::Truncated;
            else if (a == "--percent" && (*v == "divide" || *v == "of-value"))
                c.percent = *v == "of-value" ? Conventions::Percent::OfValue : Conventions::Percent::Divide;
            else {
                const char* values = a == "--log" ? "10 or e" : a == "--mod" ? "truncated or floored" : "divide or of-value";
                err << "calc: " << a << " takes " << values << "\n";
                return 2;
            }
            continue;
        }
        if (a == "--color") {
            const auto v = value();
            if (!v) return 2;
            if (*v != "auto" && *v != "always" && *v != "never") {
                err << "calc: --color takes auto, always or never\n";
                return 2;
            }
            colorWhen = *v;
            continue;
        }
        if (a.size() > 2 && a.compare(0, 2, "--") == 0) {
            err << "calc: unknown option '" << a << "'\nTry 'calc --help'.\n";
            return 2;
        }
        expressions.push_back(a);
    }
    s.color = !s.json && (colorWhen == "always" || (colorWhen == "auto" && terminal));
    Session session;
    bool ok = true;
    if (!expressions.empty()) {
        for (const std::string& e : expressions) ok = handle(e, session, s, out, err) && ok;
    } else {
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            ok = handle(line, session, s, out, err) && ok;
        }
    }
    return ok ? 0 : 1;
}

}  // namespace calc
