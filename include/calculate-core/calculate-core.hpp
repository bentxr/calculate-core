#pragma once

#include <atomic>
#include <cstddef>
#include <string>

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

}  // namespace calculate_core
