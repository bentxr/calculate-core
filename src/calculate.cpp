#include <calculate-core/calculate-core.hpp>

#include "engine.hpp"
#include "parser.hpp"

#include <utility>

namespace calculate_core {

namespace {

using namespace detail;

int decimalDigitsOf(int bits) { return static_cast<int>((static_cast<long long>(bits) * 30103 + 50000) / 100000); }

template <class T>
TypeInfo describe(NumberType type, std::string label, std::string cppName, int storageBits, std::string note) {
    if constexpr (isExact<T>) {
        return {type, std::move(label), std::move(cppName), 0, 0, 0, std::move(note)};
    } else {
        const int p = precisionBits<T>();
        return {type, std::move(label), std::move(cppName), storageBits, p, decimalDigitsOf(p), std::move(note)};
    }
}

int longDoubleStorageBits() {
    const int p = precisionBits<long double>();
    return p == 64 ? 80 : p == 113 ? 128 : p == 53 ? 64 : static_cast<int>(sizeof(long double) * 8);
}

// Never hide a collapse between types: label it.
std::string longDoubleNote() {
    if (precisionBits<long double>() == precisionBits<Binary128>() && maxExponent<long double>() == maxExponent<Binary128>())
        return "same format as binary128 here";
    if (precisionBits<long double>() == precisionBits<double>()) return "identical to double here";
    return "";
}

}  // namespace

std::vector<TypeInfo> numberTypes() {
    const std::string software = "software, no subnormals";
    return {
        describe<float>(NumberType::Float, "Single", "float", 32, ""),
        describe<double>(NumberType::Double, "Double", "double", 64, ""),
        describe<long double>(NumberType::LongDouble, "Extended", "long double", longDoubleStorageBits(), longDoubleNote()),
        describe<Rational>(NumberType::Exact, "Exact", "cpp_rational", 0, "no rounding"),
        describe<Binary128>(NumberType::Binary128, "Quadruple", "binary128", 128, software),
        describe<Binary256>(NumberType::Binary256, "Octuple", "binary256", 256, software),
        describe<Binary512>(NumberType::Binary512, "Binary512", "binary512", 512, software),
    };
}

}  // namespace calculate_core
