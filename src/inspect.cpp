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

}  // namespace

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
