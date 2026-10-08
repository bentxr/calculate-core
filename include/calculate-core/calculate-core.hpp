#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
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
    UncertainDiscreteArgument, ArgumentNearJump, UnknownTarget, TooManyTerms, ReservedName, ArgumentNearEdge, Cancelled
};

// begin/end: byte offsets of the offending part of the expression, [begin, end).
struct Error {
    ErrorCode code;
    std::string message;
    std::size_t begin = 0;
    std::size_t end = 0;
};

// How the words whose meaning differs between traditions are read. Results and stored texts (Ans, M,
// variables) are written in a canonical spelling (log10, ln, rem, floormod, the percentage written out), so
// changing a convention never changes what an earlier result means.
struct Conventions {
    enum class Log { Base10, Natural };
    enum class Mod { Truncated, Floored };
    enum class Percent { Divide, OfValue };
    Log log = Log::Base10;
    Mod mod = Mod::Truncated;
    Percent percent = Percent::Divide;
};

struct Options {
    NumberType type = NumberType::Double;
    AngleUnit angle = AngleUnit::Radians;
    Conventions conventions;
    // Accept arguments whose error reaches a jump, an edge or a whole-number requirement; the bound is then incomplete.
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

// A function of the language, for keypads: its name, arity (-1: any) and Exact availability.
struct FunctionDescription {
    std::string name;
    int minArgs;
    int maxArgs;
    bool exact;
};

std::vector<FunctionDescription> functions();

enum class WarningCode { EmptyRange };  // grows with each producer

// Something worth knowing about a result that is not an error. begin/end: bytes of the expression.
struct Warning {
    WarningCode code;
    std::string message;
    std::size_t begin = 0;
    std::size_t end = 0;
};

// What "to <target>" made of a result: the same value in another form.
struct Conversion {
    std::string target;  // the target's name, "fraction"
    std::string text;    // the converted result as plain text, "3602879701896397/36028797018963968"
};

// A conversion target, for completion and keys.
struct TargetDescription {
    std::string name;
    std::string summary;  // one line, English
};

std::vector<TargetDescription> conversionTargets();

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
    std::string bound;                // guaranteed bound: input + rounding + library
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
    std::string comment;              // the text after '#', "" when none
    bool commentOnly = false;         // the input was only a comment: a note with no value
    std::optional<Conversion> conversion;  // set when the input ended in "to <target>"
    std::vector<Warning> warnings;         // notes about a result that is not an error
    std::string assigned;                  // the variable set by "name := …", "" otherwise
};

Result evaluate(std::string_view expression, const Options& options = {});

// History, Ans and memory. Ans and M are stored as expression text, so a later evaluation in
// another type recomputes them in that type, with their error analysis intact.
class Session {
public:
    struct Entry {
        std::string input;
        Result result;
    };

    Result evaluate(std::string_view expression, const Options& options = {});
    // Like evaluate(), with Ans and M, but nothing changes: for showing a result while it is being typed.
    Result preview(std::string_view expression, const Options& options = {}) const;
    bool memoryAdd();       // M = M + Ans; false when there is no Ans
    bool memorySubtract();  // M = M - Ans; false when there is no Ans
    bool memoryStore();     // M = Ans; false when there is no Ans
    void memoryClear();
    const std::string& answer() const { return answer_; }
    const std::string& memory() const { return memory_; }
    const std::vector<Entry>& history() const { return history_; }
    void clearHistory() { history_.clear(); }

    using Variables = std::map<std::string, std::string, std::less<>>;
    const Variables& variables() const { return variables_; }  // name → expression text, names expanded
    bool forget(std::string_view name);                         // false when there was no such variable
    void clearVariables() { variables_.clear(); }

private:
    Variables variables_;
    std::string answer_;
    std::string memory_;
    std::vector<Entry> history_;
};

}  // namespace calculate_core
