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
           "  --json             one JSON object per expression\n"
           "  --color <when>     auto (default), always or never\n"
           "  --allow-uncertain  let discrete functions take arguments that carry error\n"
           "  --list-types       describe the number types of this build\n"
           "  --help, --version\n"
           "\n"
           "Lines M+, M- and MC add Ans to, subtract it from, or clear the memory M.\n";
}

struct Settings {
    Options options;
    bool json = false;
    bool color = false;
};

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

void printHuman(std::ostream& out, const std::string& input, const Result& r, bool color) {
    out << input << "\n";
    if (r.exact) {
        out << "= " << formatFraction(*r.exact) << "\n";
        out << "  exact, no rounding error · κ " << r.conditionNumber << "\n";
        return;
    }
    out << "= " << formatValue(r.value, r.trustedDigits, color) << "\n";
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
        out << (session.memory().empty() ? std::string("M cleared") : "M = " + session.memory()) << "\n";
        return true;
    }
    const Result r = session.evaluate(line, s.options);
    if (r.error) printError(err, line, *r.error);
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
