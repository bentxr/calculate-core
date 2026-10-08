#include "inspect.hpp"

#include <cctype>

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

std::string languageName(FloatFormat format) {
    switch (format) {
    case FloatFormat::Binary16: return "fp16";
    case FloatFormat::Bfloat16: return "bf16";
    case FloatFormat::Binary32: return "fp32";
    case FloatFormat::Binary64: return "fp64";
    case FloatFormat::X87Extended: return "fp80";
    case FloatFormat::Binary128: return "fp128";
    case FloatFormat::Binary256: return "fp256";
    case FloatFormat::Binary512: return "fp512";
    }
    return "";
}

FloatFormatInfo row(FloatFormat format, std::optional<NumberType> type, bool subnormals) {
    const BinaryFormat& f = binaryFormat(format);
    return {format,         type,       formatName(format), f.storageBits(),      f.exponentBits, f.fractionBits,
            f.precision(), f.bias(),   f.explicitLeadingBit, subnormals,          languageName(format)};
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

FloatInspection inspectValue(const FloatFormatInfo& info, const FloatValue& v) {
    using calculate_core::FloatClass;
    const BinaryFormat& f = binaryFormat(info.format);
    FloatInspection r;
    r.format = info.format;
    r.stored = bitsOf(f, v, encode(f, v));
    const bool finite = v.kind == FloatClass::Zero || v.kind == FloatClass::Subnormal || v.kind == FloatClass::Normal;
    if (finite || v.kind == FloatClass::Infinite) {
        r.hasNeighbours = true;
        const FloatValue below = nextDown(f, info.subnormals, v);
        const FloatValue above = nextUp(f, info.subnormals, v);
        r.below = bitsOf(f, below, encode(f, below));
        r.above = bitsOf(f, above, encode(f, above));
    }
    if (finite) {
        r.ulpExponent = ulpExponent(f, info.subnormals, v);
        r.ulp = digitsOf(terminatingDigits(scaleByPowerOfTwo(Rational(1), r.ulpExponent)));
    }
    return r;
}

const char* className(calculate_core::FloatClass c) {
    using calculate_core::FloatClass;
    switch (c) {
    case FloatClass::Zero: return "zero";
    case FloatClass::Subnormal: return "subnormal";
    case FloatClass::Normal: return "normal";
    case FloatClass::Infinite: return "infinite";
    case FloatClass::QuietNaN: return "quiet NaN";
    case FloatClass::SignalingNaN: return "signaling NaN";
    case FloatClass::Noncanonical: return "noncanonical";
    }
    return "";
}

Conversion conversionOf(const FloatFormatInfo& info, const FloatValue& v, const std::optional<Rational>& typed) {
    using calculate_core::FloatClass;
    FloatInspection i = inspectValue(info, v);
    const FloatBits& b = i.stored;
    Conversion c;
    c.target = info.languageName;
    c.text = b.sign + " " + b.exponent + " " + b.fraction;
    c.fields.push_back({"hex", "0x" + b.hex});
    c.fields.push_back({"class", className(b.valueClass)});
    c.fields.push_back({"stored", exactText(b)});
    const bool finite = b.valueClass == FloatClass::Zero || b.valueClass == FloatClass::Subnormal || b.valueClass == FloatClass::Normal;
    if (finite && typed) {
        const Rational stored = v.negative ? Rational(-v.magnitude) : v.magnitude;
        const Digits error = digitsOf(terminatingDigits(stored - *typed));
        // an error is small: always d.ddd…e±N, so its size reads at a glance
        std::string text = error.digits.substr(0, 1);
        if (error.digits.size() > 1) text += "." + error.digits.substr(1);
        if (error.digits != "0")
            text += "e" + std::string(error.exponent10 < 0 ? "-" : "+") + std::to_string(error.exponent10 < 0 ? -error.exponent10 : error.exponent10);
        c.fields.push_back({"error", (error.negative ? "-" : "+") + text});
    }
    if (finite) c.fields.push_back({"ulp", "2^" + std::to_string(i.ulpExponent)});
    if (i.hasNeighbours) {
        c.fields.push_back({"below", exactText(i.below)});
        c.fields.push_back({"above", exactText(i.above)});
    }
    std::string note;
    if (typed && *typed != 0 && b.valueClass == FloatClass::Infinite) note = "overflow";
    if (typed && *typed != 0 && b.valueClass == FloatClass::Zero) note = "underflow";
    if (!note.empty()) c.fields.push_back({"note", note});
    return c;
}

// The inspector's row a `to` name stands for: fp16 … fp512 and their IEEE names; fp128 is the Quadruple type's row.
std::optional<FloatFormatInfo> formatNamed(std::string_view name) {
    struct Name {
        std::string_view language, ieee;
        FloatFormat format;
    };
    static const Name names[] = {{"fp16", "binary16", FloatFormat::Binary16},   {"bf16", "bfloat16", FloatFormat::Bfloat16},
                                 {"fp32", "binary32", FloatFormat::Binary32},   {"fp64", "binary64", FloatFormat::Binary64},
                                 {"fp80", "x87", FloatFormat::X87Extended},     {"fp128", "binary128", FloatFormat::Binary128},
                                 {"fp256", "binary256", FloatFormat::Binary256}, {"fp512", "binary512", FloatFormat::Binary512}};
    for (const Name& n : names) {
        if (name != n.language && name != n.ieee) continue;
        std::optional<FloatFormatInfo> found;
        for (const FloatFormatInfo& f : floatFormats())
            if (f.format == n.format && (!found || f.type == NumberType::Binary128)) found = f;
        return found;
    }
    return std::nullopt;
}

// The exact value fromBits stands for: the pattern (0x… or 0b…) read in the named format (empty: `type`'s own).
std::optional<Rational> bitsLiteral(const std::string& format, const std::string& pattern, NumberType type, Error& error) {
    using calculate_core::FloatClass;
    std::optional<FloatFormatInfo> info;
    if (format.empty()) {
        if (type == NumberType::Exact) {
            error = Error{ErrorCode::NotAvailableInExact, "Exact has no binary format: name one, e.g. fromBits(…, fp64)", 0, 0};
            return std::nullopt;
        }
        info = formatInfo(type);
    } else {
        info = formatNamed(format);
        if (!info) {
            error = Error{ErrorCode::UnknownName, "Unknown format '" + format + "' (see calc --list-formats)", 0, 0};
            return std::nullopt;
        }
    }
    const bool hex = pattern.size() > 1 && (pattern[1] == 'x' || pattern[1] == 'X');
    const FloatInspection r = inspectBits(*info, pattern, hex ? 16 : 2);
    if (r.error) {
        error = *r.error;
        return std::nullopt;
    }
    const FloatBits& b = r.stored;
    if (b.valueClass != FloatClass::Zero && b.valueClass != FloatClass::Subnormal && b.valueClass != FloatClass::Normal) {
        error = Error{ErrorCode::LiteralOutOfRange, "That pattern is " + std::string(className(b.valueClass)) + " in " + info->name
                                                        + ": the calculator computes with finite numbers", 0, 0};
        return std::nullopt;
    }
    const FloatValue v = decode(binaryFormat(info->format), [&] {
        Integer n = 0;
        for (std::size_t i = 2; i < pattern.size(); ++i) {
            const char c = pattern[i];
            n = n * (hex ? 16 : 2) + (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        }
        return n;
    }());
    return v.negative ? Rational(-v.magnitude) : v.magnitude;
}

FloatFormatInfo formatInfo(NumberType type) {
    for (const FloatFormatInfo& f : floatFormats())
        if (f.type == type) return f;
    return floatFormats()[3];  // Double
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
    r = inspectValue(format, v);
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

FloatInspection inspectBits(const FloatFormatInfo& format, std::string_view digits, int base) {
    using calculate_core::FloatClass;
    FloatInspection r;
    r.format = format.format;
    const std::string what = base == 2 ? "Not a binary number" : "Not a hexadecimal number";
    std::string s;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (digits.substr(i, 3) == "\xE2\x80\x89") {  // a thin space
            i += 2;
        } else if (digits[i] != ' ' && digits[i] != '_') {
            s += digits[i];
        }
    }
    const std::string prefix = base == 2 ? "0b" : "0x";
    if (s.size() >= 2 && s[0] == '0' && (s[1] == prefix[1] || s[1] == std::toupper(prefix[1]))) s.erase(0, 2);
    Integer pattern = 0;
    for (const char c : s) {
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0 || d >= base) {
            r.error = Error{ErrorCode::InvalidNumber, what, 0, digits.size()};
            return r;
        }
        pattern = pattern * base + d;
    }
    if (s.empty()) {
        r.error = Error{ErrorCode::InvalidNumber, what, 0, digits.size()};
        return r;
    }
    const BinaryFormat& f = binaryFormat(format.format);
    if (pattern != 0 && static_cast<int>(msb(pattern)) + 1 > f.storageBits()) {
        r.error = Error{ErrorCode::LiteralOutOfRange, format.name + " has " + std::to_string(f.storageBits()) + " bits", 0, digits.size()};
        return r;
    }
    const FloatValue v = decode(f, pattern);
    if (v.kind == FloatClass::Subnormal && !format.subnormals) {  // an IEEE pattern this type never produces
        FloatFormatInfo ieee = format;
        ieee.subnormals = true;
        r = inspectValue(ieee, v);
        r.format = format.format;
        r.note = "no subnormals";
        return r;
    }
    return inspectValue(format, v);
}

std::string exactText(const Digits& d) {
    const std::string& sig = d.digits;
    const long long n = static_cast<long long>(sig.size());
    const long long e = d.exponent10;
    std::string text = d.negative ? "-" : "";
    if (e >= -7 && e < 21) {  // positional
        if (e < 0) return text + "0." + std::string(static_cast<std::size_t>(-e - 1), '0') + sig;
        for (long long i = 0; i <= e; ++i) text += i < n ? sig[static_cast<std::size_t>(i)] : '0';
        if (n > e + 1) text += "." + sig.substr(static_cast<std::size_t>(e) + 1);
        return text;
    }
    text += sig.substr(0, 1);
    if (n > 1) text += "." + sig.substr(1);
    return text + "e" + (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
}

std::string exactText(const FloatBits& b) {
    using calculate_core::FloatClass;
    if (b.valueClass == FloatClass::Infinite) return b.negative ? "-inf" : "inf";
    if (b.valueClass == FloatClass::QuietNaN || b.valueClass == FloatClass::SignalingNaN) return "nan";
    if (!b.value.digits.empty()) return exactText(b.value);
    const std::string sign = b.negative ? "-" : "";
    if (b.significand.digits == "1" && b.significand.exponent10 == 0) return sign + "2^" + std::to_string(b.exponent2);
    return sign + exactText(b.significand) + " \xC3\x97 2^" + std::to_string(b.exponent2);
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
