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

// What a floating-point bit pattern is. Noncanonical: an x87 extended pattern the 387 and later never produce.
enum class FloatClass { Zero, Subnormal, Normal, Infinite, QuietNaN, SignalingNaN, Noncanonical };

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

// Which combination of the uncertain inputs leads (both are always computed): the worst case Σ|∂f/∂x|·u (a limit),
// or the statistical √Σ(∂f/∂x·u)² (an estimate).
enum class UncertaintyRule { Linear, Quadrature };

// Read precision: typed numbers carry half a unit of their last digit as uncertainty.
enum class ReadPrecision { Off, Decimals, All };  // Decimals: only numbers written with a point

struct Options {
    NumberType type = NumberType::Double;
    AngleUnit angle = AngleUnit::Radians;
    Conventions conventions;
    // Accept arguments whose error reaches a jump, an edge or a whole-number requirement; the bound is then incomplete.
    bool allowUncertainDiscreteArguments = false;
    const std::atomic<bool>* cancel = nullptr;
    UncertaintyRule uncertaintyRule = UncertaintyRule::Linear;
    ReadPrecision readPrecision = ReadPrecision::Off;
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

// What an argument stands for: argument hints and generated keys use it. An Angle follows the angle unit; an
// Integer must be whole.
enum class ArgumentKind { Number, Integer, Angle };

struct ArgumentDescription {
    std::string name;  // "x", "n", "base"
    ArgumentKind kind;
};

// A function of the language, for keypads, completion and help. Arguments beyond minArgs are optional.
struct FunctionDescription {
    std::string name;
    int minArgs;
    int maxArgs;                                 // -1: any number
    bool exact;                                  // available in the Exact type
    std::string title{};                         // "Inverse sine" (English; the app translates it)
    std::string description{};                   // one sentence, ending with a full stop
    std::vector<ArgumentDescription> arguments{};  // maxArgs entries; one entry, repeated, when maxArgs is -1
    std::string example{};                       // "asin(0.5)": evaluates without error in Double
    std::string category{};                      // one of functionCategories()
    std::vector<std::string> aliases{};          // other spellings the parser accepts ("arcsin", "arcsen")
};

std::vector<FunctionDescription> functions();

// A named value of the language, for keypads and lists.
struct ConstantDescription {
    std::string name;         // as typed: "G", "phi", "dozen"
    std::string title;        // English: "Newtonian constant of gravitation", "golden ratio", "one dozen (12)"
    std::string category;     // "mathematical", "number name" or "physical"
    std::string value;        // as published: "6.67430e-11", "1.054571817...e-34", "12"; "" for irrational ones
    std::string uncertainty;  // CODATA's standard uncertainty: "0.00015e-11"; "" when exact
    std::string limit;        // the ± the calculator uses, three standard uncertainties: "4.5e-15"; "" when exact
    std::string unit;         // the SI unit ("m³·kg⁻¹·s⁻²"), NIST's text when not SI ("MeV"); "" for pure numbers
    bool exact = false;       // usable in the Exact type
    std::string group;        // physical ones, for lists: "Universal", "Electromagnetic", "Particle masses"…; "" otherwise
};

std::vector<ConstantDescription> constants();
std::vector<std::string> functionCategories();  // in display order

enum class WarningCode { EmptyRange, FirstOrderUnreliable, UnitsDiffer };  // grows with each producer

// Something worth knowing about a result that is not an error. begin/end: bytes of the expression.
struct Warning {
    WarningCode code;
    std::string message;
    std::size_t begin = 0;
    std::size_t end = 0;
};

// A number as a target shows it, split where the trusted digits end.
struct NumberParts {
    bool negative = false;
    std::string trusted;       // "1.000000000000000": the sign is apart, the point included
    std::string noise;         // "055511151231257827021181583404541015625"; empty when every digit is trusted
    long long exponent10 = 0;  // shown as e±n when hasExponent
    bool hasExponent = false;
    std::string suffix;        // "%" (to percent)
};

// What "to <target>" made of a result: the same value in another form.
struct Conversion {
    std::string target;  // the target's name, "fraction"
    std::string text;    // the converted result as plain text, "3602879701896397/36028797018963968"
    std::optional<NumberParts> parts;  // set by targets that show a number
    std::string note;  // "off by 3.3e-2": the stored value minus what the text shows, when they differ
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

// One uncertain input of a result.
struct UncertainInput {
    std::string name;          // as written: "5±0.2", "G", "1.1", "Ans"
    std::string uncertainty;   // its own u: "2e-1"
    std::string sensitivity;   // |∂result/∂input|: "1e+0"
    std::string contribution;  // sensitivity × u: "2e-1"
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
    std::string reading;                   // how the expression was read: every operation in parentheses
    std::vector<UncertainInput> uncertainInputs;  // largest contribution first; empty when every input is exact
    std::string uncertaintyLinear;      // worst case, "" when there are no uncertain inputs
    std::string uncertaintyQuadrature;  // statistical, "" likewise
    UncertaintyRule uncertaintyRule = UncertaintyRule::Linear;  // the one that leads (from the options)
    int trustedDigitsWithUncertainty = 0;  // by the bound plus the leading uncertainty (floating types)
    bool firstOrderChecked = false;        // the corners were evaluated
    bool firstOrderReliable = true;        // false: the first-order figure misjudges this uncertainty
    std::string firstOrderObserved;        // the largest change at the corners, "inf" if one failed; "" if unchecked
    Digits uncertaintyShown;  // bound + the leading uncertainty, two significant digits: {false, "20", -1} is 0.20
    std::string concise;      // "5.00(20)"; "" when there is neither error nor uncertainty
    std::string plusMinus;    // "5.00 ± 0.20"
    std::string unit;         // SI unit of the result ("m·s⁻¹"); "" when it has none or cannot be told
    bool unitKnown = true;    // false: units that differ, or a constant in a unit outside the SI
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
