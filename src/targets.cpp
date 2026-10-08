#include "targets.hpp"

namespace calculate_core::detail {

namespace {

// The stored value as the exact fraction it is: what the number type really holds.
std::optional<Error> fraction(const TargetInput& in, Result& result) {
    const TargetText& target = *in.parsed.target;
    if (!target.argument.empty())
        return Error{ErrorCode::UnexpectedToken, "fraction takes nothing after it", target.span.begin, target.span.end};
    std::string text = (in.value < 0 ? "-" : "") + Integer(abs(numerator(in.value))).str();
    if (denominator(in.value) != 1) text += "/" + denominator(in.value).str();
    result.conversion = Conversion{"fraction", text, std::nullopt};
    return std::nullopt;
}

// More digits than this are not written out (an exact value's period, or a huge or tiny one's digits).
constexpr std::size_t maxDigits = 20000;

std::string joined(const NumberParts& p) {
    std::string text = (p.negative ? "-" : "") + p.trusted;
    if (!p.noise.empty()) text += "|" + p.noise;
    if (p.hasExponent) text += std::string(p.exponent10 < 0 ? "e-" : "e+") + std::to_string(p.exponent10 < 0 ? -p.exponent10 : p.exponent10);
    return text + p.suffix;
}

// An exact value's digits, all trusted: the integer part, the digits before the period, and the period (repeating
// forever), from the long division. Scientific and engineering start at the first significant digit, so a period
// that begins before the point is rotated to begin right after it: 1/7 = 1.(428571)e-1.
std::optional<NumberParts> exactParts(const Rational& q, Notation notation) {
    const FractionDigits f = exactFraction(q, static_cast<int>(maxDigits));
    if (!f.hasDecimal || f.integerPart.size() + f.fractionDigits.size() + f.repeatingDigits.size() > maxDigits) return std::nullopt;
    NumberParts p;
    p.negative = f.negative;
    if (notation == Notation::Positional) {
        p.trusted = f.integerPart;
        if (!f.fractionDigits.empty() || !f.repeatingDigits.empty()) p.trusted += "." + f.fractionDigits;
        if (!f.repeatingDigits.empty()) p.trusted += "(" + f.repeatingDigits + ")";
        return p;
    }
    // Every digit in order (the integer part's, then the fraction's), and where the period starts.
    std::string all = f.integerPart + f.fractionDigits;
    const std::size_t periodStart = f.repeatingDigits.empty() ? std::string::npos : all.size();
    const std::string& period = f.repeatingDigits;
    const auto digitAt = [&](std::size_t i) {
        return i < all.size() ? all[i] : period[(i - all.size()) % period.size()];
    };
    std::size_t first = 0;  // the first significant digit; a value that is not 0 has one within a period's reach
    const std::size_t limit = all.size() + period.size();
    while (first < limit && digitAt(first) == '0') ++first;
    if (first == limit) return NumberParts{p.negative, "0", "", 0, true, ""};
    const long long e = static_cast<long long>(f.integerPart.size()) - 1 - static_cast<long long>(first);
    const long long shift = notation == Notation::Engineering ? ((e % 3) + 3) % 3 : 0;
    const std::size_t point = first + static_cast<std::size_t>(shift) + 1;  // index in `all` (extended) of the first digit after the point
    p.hasExponent = true;
    p.exponent10 = e - shift;
    for (std::size_t i = first; i < point; ++i) p.trusted += digitAt(i);
    if (periodStart == std::string::npos) {  // a terminating expansion: the rest of its digits, without trailing zeros
        std::string rest = point < all.size() ? all.substr(point) : "";
        rest.erase(rest.find_last_not_of('0') + 1);
        if (!rest.empty()) p.trusted += "." + rest;
        return p;
    }
    std::string before;  // the digits after the point that come before the period
    for (std::size_t i = point; i < periodStart; ++i) before += digitAt(i);
    std::string block;   // the period, starting wherever it stands after the point
    const std::size_t from = std::max(point, periodStart);
    for (std::size_t i = from; i < from + period.size(); ++i) block += digitAt(i);
    while (!before.empty() && before.back() == block.back()) {  // start the period as early as it can: 3.(3), not 3.3(3)
        block = before.back() + block.substr(0, block.size() - 1);
        before.pop_back();
    }
    p.trusted += "." + before + "(" + block + ")";
    return p;
}

// Every digit in a notation, with the bar where the trusted digits end.
std::optional<Error> notation(const TargetInput& in, Result& result, Notation n) {
    const TargetText& target = *in.parsed.target;
    if (!target.argument.empty())
        return Error{ErrorCode::UnexpectedToken, target.name + " takes nothing after it", target.span.begin, target.span.end};
    NumberParts parts;
    if (in.options.type == NumberType::Exact) {
        const std::optional<NumberParts> exact = exactParts(in.value, n);
        if (!exact) return Error{ErrorCode::UnexpectedToken, "too many digits to write out: use to fraction", target.span.begin, target.span.end};
        parts = *exact;
    } else {
        const DecimalDigits d = exactDigits(in.value);
        if (d.digits.size() + static_cast<std::size_t>(d.exponent10 < 0 ? -d.exponent10 : d.exponent10) > maxDigits && n == Notation::Positional)
            return Error{ErrorCode::UnexpectedToken, "too many digits to write out: use to fraction", target.span.begin, target.span.end};
        parts = formatParts(d, result.trustedDigits, n);
    }
    result.conversion = Conversion{target.name, joined(parts), parts};
    return std::nullopt;
}

std::optional<Error> scientific(const TargetInput& in, Result& result) { return notation(in, result, Notation::Scientific); }
std::optional<Error> engineering(const TargetInput& in, Result& result) { return notation(in, result, Notation::Engineering); }
std::optional<Error> positional(const TargetInput& in, Result& result) { return notation(in, result, Notation::Positional); }

}  // namespace

const std::vector<Target>& targets() {
    static const std::vector<Target> list{
        {"fraction", "the exact fraction the result is stored as", fraction},
        {"sci", "every digit, in scientific notation", scientific},
        {"eng", "every digit, with an exponent that is a multiple of 3", engineering},
        {"simple", "every digit, without an exponent", positional},
    };
    return list;
}

const Target* findTarget(std::string_view name) {
    for (const Target& t : targets())
        if (t.name == name) return &t;
    return nullptr;
}

}  // namespace calculate_core::detail
