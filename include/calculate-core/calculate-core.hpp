#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
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

// value = (negative ? -1 : 1) * d1.d2d3... * 10^exponent10: every digit, nothing truncated.
struct Digits {
    bool negative = false;
    std::string digits;
    long long exponent10 = 0;
};

// An exact rational. When hasDecimal: integerPart.fractionDigits(repeatingDigits repeated).
struct Fraction {
    bool negative = false;
    std::string numerator;
    std::string denominator;
    bool hasDecimal = false;
    std::string integerPart;
    std::string fractionDigits;
    std::string repeatingDigits;
};

struct Result {
    std::optional<Error> error;
    NumberType type = NumberType::Double;
    Digits value;                     // floating types; empty for Exact
    std::optional<Fraction> exact;    // Exact only
    int trustedDigits = 0;            // leading significant digits guaranteed by the bound
    int trustedDigitsMeasured = 0;    // leading significant digits confirmed by the measured error
    std::string bound;                // guaranteed first-order bound: input + rounding + library
    std::string inputError;
    std::string roundingError;
    std::string libraryError;
    std::string measured;             // |value - reference|, "" when unavailable
    std::string conditionNumber;
    bool measuredAvailable = false;
    bool measurementReliable = false;
    bool boundComplete = true;        // false when uncertain discrete arguments were allowed
    int roundingOperations = 0;       // operations whose result was actually rounded
    std::string expression;           // what was evaluated, with Ans and M expanded
};

Result evaluate(std::string_view expression, const Options& options = {});

}  // namespace calculate_core
