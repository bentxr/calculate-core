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
    result.conversion = Conversion{"fraction", text, std::nullopt, ""};
    return std::nullopt;
}

// The value with its error and the leading uncertainty, as people write them: 5.00(20) or 5.00 ± 0.20.
std::optional<Error> uncertain(const TargetInput& in, Result& result, bool concise) {
    const TargetText& target = *in.parsed.target;
    if (!target.argument.empty())
        return Error{ErrorCode::UnexpectedToken, target.name + " takes nothing after it", target.span.begin, target.span.end};
    const Uncertainty& u = in.report.uncertainty;
    const Ruler lead = in.options.uncertaintyRule == UncertaintyRule::Linear ? u.linear : u.quadrature;
    const UncertainForms f = uncertainForms(in.value, in.report.bound + lead);
    const std::string& text = concise ? f.concise : f.plusMinus;
    if (text.empty())
        return Error{ErrorCode::UnexpectedToken, "nothing to show: the result has no error and no uncertainty", target.span.begin,
                     target.span.end};
    result.conversion = Conversion{concise ? "concise" : "\xC2\xB1", text, std::nullopt, ""};
    return std::nullopt;
}

std::optional<Error> concise(const TargetInput& in, Result& result) { return uncertain(in, result, true); }
std::optional<Error> plusMinus(const TargetInput& in, Result& result) { return uncertain(in, result, false); }

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
    result.conversion = Conversion{target.name, joined(parts), parts, ""};
    return std::nullopt;
}

// A whole number and a fraction: 2 + 1/3, with the sign outside, -(2 + 1/3).
std::optional<Error> mixed(const TargetInput& in, Result& result) {
    const TargetText& target = *in.parsed.target;
    if (!target.argument.empty())
        return Error{ErrorCode::UnexpectedToken, "mixed takes nothing after it", target.span.begin, target.span.end};
    const Rational q = abs(in.value);
    const Integer whole = numerator(q) / denominator(q);
    const Rational rest = q - Rational(whole);
    const std::string fractionText = numerator(rest).str() + "/" + denominator(rest).str();
    std::string text = rest == 0 ? whole.str() : whole == 0 ? fractionText : whole.str() + " + " + fractionText;
    if (in.value < 0) text = whole != 0 && rest != 0 ? "-(" + text + ")" : "-" + text;
    result.conversion = Conversion{"mixed", text, std::nullopt, ""};
    return std::nullopt;
}

// The value × 100 with every digit and %: the scaling moves the point, not the trust.
std::optional<Error> percent(const TargetInput& in, Result& result) {
    const TargetText& target = *in.parsed.target;
    if (!target.argument.empty())
        return Error{ErrorCode::UnexpectedToken, "percent takes nothing after it", target.span.begin, target.span.end};
    const Rational scaled = in.value * 100;
    NumberParts parts;
    if (in.options.type == NumberType::Exact) {
        const std::optional<NumberParts> exact = exactParts(scaled, Notation::Positional);
        if (!exact) return Error{ErrorCode::UnexpectedToken, "too many digits to write out: use to fraction", target.span.begin, target.span.end};
        parts = *exact;
    } else {
        parts = formatParts(exactDigits(scaled), result.trustedDigits, Notation::Positional);
    }
    parts.suffix = "%";
    result.conversion = Conversion{"percent", joined(parts), parts, ""};
    return std::nullopt;
}

// The nearest fraction k/n with the denominator asked for, and how far the stored value is from it.
std::optional<Error> fixedDenominator(const TargetInput& in, Result& result) {
    const TargetText& target = *in.parsed.target;
    const std::string digits = target.name.substr(2);
    const bool whole = !target.argument.empty() ? false
                       : !digits.empty() && digits.size() <= 10 && digits.find_first_not_of("0123456789") == std::string::npos;
    if (!whole || std::stoll(digits) < 1 || std::stoll(digits) > 1000000000)
        return Error{ErrorCode::UnexpectedToken, "1/n needs a whole n from 1 to 1000000000", target.span.begin, target.span.end};
    const Integer n(std::stoll(digits));
    const Rational scaled = abs(in.value) * Rational(n);
    Integer k = numerator(scaled + Rational(1, 2)) / denominator(scaled + Rational(1, 2));  // halves away from zero
    if (in.value < 0) k = -k;
    const Rational shown(k, n);
    const Rational off = in.value - shown;
    result.conversion = Conversion{"1/n", k.str() + "/" + n.str(), std::nullopt,
                                   off == 0 ? "" : "off by " + formatScientific(fromRational<Ruler>(off))};
    return std::nullopt;
}

bool fixedDenominatorName(std::string_view s) { return s.size() > 2 && s.substr(0, 2) == "1/"; }

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
        {"mixed", "the stored value as a whole number and a fraction", mixed},
        {"percent", "the value × 100, every digit, with %", percent},
        {"1/n", "the nearest fraction with denominator n, and how far it is", fixedDenominator, fixedDenominatorName},
        {"concise", "the value and its error or uncertainty as 1.23(4)", concise},
        {"\xC2\xB1", "the value \xC2\xB1 its error or uncertainty", plusMinus},
        {"pm", "the same as \xC2\xB1", plusMinus},
    };
    return list;
}

const Target* findTarget(std::string_view name) {
    for (const Target& t : targets())
        if (t.name == name || (t.matches && t.matches(name))) return &t;
    return nullptr;
}

}  // namespace calculate_core::detail
