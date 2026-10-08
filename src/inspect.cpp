#include "inspect.hpp"

namespace calculate_core {

namespace {

using namespace detail;

std::string formatName(FloatFormat format) {
    switch (format) {
    case FloatFormat::Binary16: return "binary16";
    case FloatFormat::Bfloat16: return "bfloat16";
    case FloatFormat::Binary32: return "binary32";
    case FloatFormat::Binary64: return "binary64";
    case FloatFormat::X87Extended: return "x87 extended";
    case FloatFormat::Binary128: return "binary128";
    case FloatFormat::Binary256: return "binary256";
    case FloatFormat::Binary512: return "binary512";
    }
    return "";
}

FloatFormatInfo row(FloatFormat format, std::optional<NumberType> type, bool subnormals) {
    const BinaryFormat& f = binaryFormat(format);
    return {format, type, formatName(format), f.storageBits(), f.exponentBits, f.fractionBits, f.precision(), f.bias(),
            f.explicitLeadingBit, subnormals};
}

template <class T>
FloatFormatInfo rowOf(NumberType type) {
    return row(floatFormatOf<T>(), type, hasSubnormals<T>());
}

Digits digitsOf(const DecimalDigits& d) { return {d.negative, d.digits, d.exponent10}; }

// The low `width` bits of n as binary digits, most significant first.
std::string binary(const Integer& n, int width) {
    std::string s(static_cast<std::size_t>(width), '0');
    for (int i = 0; i < width; ++i)
        if (bit_test(n, static_cast<unsigned>(width - 1 - i))) s[static_cast<std::size_t>(i)] = '1';
    return s;
}

// Every bit as upper-case hexadecimal digits, four bits each.
std::string hexadecimal(const Integer& n, int bits) {
    static const char digits[] = "0123456789ABCDEF";
    std::string s;
    for (int i = bits / 4 - 1; i >= 0; --i) {
        int nibble = 0;
        for (int b = 3; b >= 0; --b) nibble = nibble * 2 + (bit_test(n, static_cast<unsigned>(4 * i + b)) ? 1 : 0);
        s += digits[nibble];
    }
    return s;
}

}  // namespace

namespace detail {

FloatBits bitsOf(const BinaryFormat& f, const FloatValue& v, const Integer& pattern) {
    using calculate_core::FloatClass;
    FloatBits b;
    b.valueClass = v.kind;
    b.negative = v.negative;
    const unsigned t = static_cast<unsigned>(f.fractionBits);
    b.sign = v.negative ? "1" : "0";
    const Integer one = 1;
    const Integer exponent = (pattern >> t) & ((one << static_cast<unsigned>(f.exponentBits)) - 1);
    b.exponent = binary(exponent, f.exponentBits);
    b.fraction = binary(pattern, f.fractionBits);
    b.hex = hexadecimal(pattern, f.storageBits());
    b.biasedExponent = static_cast<long long>(exponent);
    b.note = std::string(v.note);
    const bool finite = v.kind == FloatClass::Zero || v.kind == FloatClass::Subnormal || v.kind == FloatClass::Normal
                        || (v.kind == FloatClass::Noncanonical && v.magnitude != 0);
    if (v.kind == FloatClass::Zero) {
        b.value = {v.negative, "0", 0};
    } else if (finite) {
        b.exponent2 = std::max(impl::floorLog2(v.magnitude), static_cast<long long>(f.minExponent()));
        b.significand = digitsOf(terminatingDigits(scaleByPowerOfTwo(v.magnitude, -b.exponent2)));
        b.value = digitsOf(terminatingDigits(v.negative ? Rational(-v.magnitude) : v.magnitude));
    }
    return b;
}

}  // namespace detail

namespace {

// Trims spaces, and reads the U+2212 minus as '-'.
std::string plain(std::string_view text) {
    std::string s;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.substr(i, 3) == "\xE2\x88\x92") {
            s += '-';
            i += 2;
        } else if (text[i] != ' ') {
            s += text[i];
        }
    }
    return s;
}

}  // namespace

FloatInspection inspectDecimal(const FloatFormatInfo& format, std::string_view text) {
    using calculate_core::FloatClass;
    FloatInspection r;
    r.format = format.format;
    const BinaryFormat& f = binaryFormat(format.format);
    std::string s = plain(text);
    const bool negative = !s.empty() && s[0] == '-';
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) s.erase(0, 1);
    FloatValue v;
    v.negative = negative;
    std::optional<DecimalLiteral> d;
    if (s == "inf" || s == "\xE2\x88\x9E") {
        v.kind = FloatClass::Infinite;
    } else if (s == "nan") {
        v.kind = FloatClass::QuietNaN;
    } else {
        d = parseDecimal(s);
        if (!d) {
            r.error = Error{ErrorCode::InvalidNumber, "Not a decimal number", 0, text.size()};
            return r;
        }
        v = decimalToFormat(negative, *d, f, format.subnormals);
    }
    r.stored = bitsOf(f, v, encode(f, v));
    if (d && d->significand != 0) {
        if (v.kind == FloatClass::Infinite) r.note = "overflow";
        if (v.kind == FloatClass::Zero) {
            r.note = "underflow";
            // stored − typed = −typed: the literal's own digits, without building the number
            std::string digits = d->significand.str();
            long long e = d->exponent10 + static_cast<long long>(digits.size()) - 1;
            digits.erase(digits.find_last_not_of('0') + 1);
            r.conversionError = {!negative, digits, e};
        } else if (v.kind != FloatClass::Infinite) {
            const Rational typed = negative ? Rational(-toRational(*d)) : toRational(*d);
            const Rational stored = v.negative ? Rational(-v.magnitude) : v.magnitude;
            r.conversionError = digitsOf(terminatingDigits(stored - typed));
        }
    } else if (d) {
        r.conversionError = {false, "0", 0};
    }
    return r;
}

std::vector<FloatFormatInfo> floatFormats() {
    std::vector<FloatFormatInfo> list{row(FloatFormat::Binary16, std::nullopt, true), row(FloatFormat::Bfloat16, std::nullopt, true),
                                      rowOf<float>(NumberType::Float), rowOf<double>(NumberType::Double),
                                      rowOf<long double>(NumberType::LongDouble)};
    if (floatFormatOf<long double>() != FloatFormat::X87Extended)  // shown for display only where long double is not it
        list.push_back(row(FloatFormat::X87Extended, std::nullopt, true));
    list.push_back(rowOf<Binary128>(NumberType::Binary128));
    list.push_back(rowOf<Binary256>(NumberType::Binary256));
    list.push_back(rowOf<Binary512>(NumberType::Binary512));
    return list;
}

}  // namespace calculate_core
