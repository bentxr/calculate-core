#pragma once

#include "float_format.hpp"

#include <calculate-core/calculate-core.hpp>

// The IEEE 754 inspector's link between the public names and the formats.
namespace calculate_core::detail {

inline const BinaryFormat& binaryFormat(FloatFormat format) {
    switch (format) {
    case FloatFormat::Binary16: return binary16;
    case FloatFormat::Bfloat16: return bfloat16;
    case FloatFormat::Binary32: return binary32;
    case FloatFormat::Binary64: return binary64;
    case FloatFormat::X87Extended: return x87Extended;
    case FloatFormat::Binary128: return binary128;
    case FloatFormat::Binary256: return binary256;
    case FloatFormat::Binary512: return binary512;
    }
    return binary64;
}

// The public name of the format a type stores its values in (no type uses 16 bits).
template <class T>
FloatFormat floatFormatOf() {
    switch (formatOf<T>().storageBits()) {
    case 32: return FloatFormat::Binary32;
    case 80: return FloatFormat::X87Extended;
    case 128: return FloatFormat::Binary128;
    case 256: return FloatFormat::Binary256;
    case 512: return FloatFormat::Binary512;
    default: return FloatFormat::Binary64;
    }
}

// A bit pattern of f, field by field, with the value v it stands for.
FloatBits bitsOf(const BinaryFormat& f, const FloatValue& v, const Integer& pattern);

// A datum of a format as the inspector shows it: its bits, its neighbours and its ulp (finite and infinite values).
FloatInspection inspectValue(const FloatFormatInfo& info, const FloatValue& v);

}  // namespace calculate_core::detail
