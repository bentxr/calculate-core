#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

// calculate-core: a calculator engine whose results carry their error.
namespace calculate_core {

enum class NumberType { Float, Double, LongDouble, Exact, Binary128, Binary256, Binary512 };

enum class AngleUnit { Radians, Degrees, Gradians };

enum class ErrorCode {
    InvalidCharacter, InvalidNumber, UnexpectedToken, UnexpectedEnd, MissingClosingParenthesis,
    MissingOperator, UnknownName, WrongArgumentCount, NotAvailableInExact, LiteralOutOfRange,
    DivisionByZero, DomainError, Overflow, IrrationalResult, ArgumentTooLarge, NotAnInteger,
    UncertainDiscreteArgument, Cancelled
};

// begin/end: byte offsets of the offending part of the expression, [begin, end).
struct Error {
    ErrorCode code;
    std::string message;
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct Options {
    NumberType type = NumberType::Double;
    AngleUnit angle = AngleUnit::Radians;
    bool allowUncertainDiscreteArguments = false;
    const std::atomic<bool>* cancel = nullptr;
};

// One entry of the type menu, described by the type's own traits in this build.
struct TypeInfo {
    NumberType type;
    std::string label;    // "Double"
    std::string cppName;  // "double"
    int storageBits;      // 64; 0 for Exact
    int precisionBits;    // significand bits, 53; 0 for Exact
    int decimalDigits;    // about precisionBits * log10(2), 16; 0 for Exact
    std::string note;     // "", "same format as binary128 here", "software, no subnormals", "no rounding"
};

std::vector<TypeInfo> numberTypes();

}  // namespace calculate_core
