#include <calculate-core/calculate-core.hpp>

#include "catalogue.hpp"
#include "engine.hpp"
#include "float_format.hpp"
#include "inspect.hpp"
#include "parser.hpp"
#include "targets.hpp"

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

// An uncertain input's name: its source text, without parentheses that enclose all of it ("(3±0.4)" → "3±0.4").
std::string sourceName(std::string_view text, Span span) {
    std::string_view name = text.substr(span.begin, span.end - span.begin);
    while (name.size() >= 2 && name.front() == '(' && name.back() == ')') {
        int depth = 0;
        bool whole = true;  // the first '(' closes at the last byte
        for (std::size_t i = 0; i + 1 < name.size(); ++i) {
            depth += name[i] == '(' ? 1 : name[i] == ')' ? -1 : 0;
            if (depth == 0) whole = false;
        }
        if (!whole) break;
        name = name.substr(1, name.size() - 2);
    }
    return std::string(name);
}

template <class T>
Result build(const Parsed& parsed, const Options& options, std::string_view text) {
    using std::abs;
    Result r;
    r.type = options.type;
    r.uncertaintyRule = options.uncertaintyRule;
    const Evaluation<T> ev = detail::evaluate<T>(parsed.ast, options, text);
    if (ev.error) {
        r.error = ev.error;
        return r;
    }
    const Report& report = ev.report;
    if constexpr (isExact<T>) {
        const FractionDigits f = exactFraction(ev.value);
        r.exact = Fraction{f.negative, f.numerator, f.denominator, f.hasDecimal, f.integerPart, f.fractionDigits,
                           f.repeatingDigits};
    } else {
        r.stored = detail::inspectValue(detail::formatInfo(options.type), valueOf(ev.value));
        const Rational q = toRational(ev.value);
        DecimalDigits d;
        if (impl::decimalLengthEstimate(q) > shownDigitsLimit) {  // millions of digits: exactly, in binary instead
            const BinaryForm b = binaryForm(q);
            r.binaryValue = BinaryValue{b.negative, b.significand, b.exponent2};
            d.negative = q < 0;
        } else {
            d = exactDigits(ev.value);
        }
        r.value = Digits{d.negative, d.digits, d.exponent10};
        const Ruler magnitude = abs(exactCast<Ruler>(ev.value));
        const int count = static_cast<int>(d.digits.size());
        r.trustedDigits = trustedDigits(magnitude, report.bound, count);
        if (report.measuredAvailable) r.trustedDigitsMeasured = trustedDigits(magnitude, report.measured, count);
        const Uncertainty& u = report.uncertainty;
        const Ruler lead = options.uncertaintyRule == UncertaintyRule::Linear ? u.linear : u.quadrature;
        r.trustedDigitsWithUncertainty = trustedDigits(magnitude, report.bound + lead, count);
    }
    r.bound = formatScientific(report.bound);
    r.inputError = formatScientific(report.input);
    r.roundingError = formatScientific(report.rounding);
    r.libraryError = formatScientific(report.library);
    if (report.measuredAvailable) r.measured = formatScientific(report.measured);
    r.conditionNumber = formatScientific(report.condition);
    r.measuredAvailable = report.measuredAvailable;
    r.measurementReliable = report.reliable;
    r.boundComplete = report.boundComplete;
    r.roundingOperations = report.roundingOperations;
    r.expression = parsed.expanded;
    r.comment = parsed.comment;
    r.warnings = parsed.warnings;
    r.assigned = parsed.assigned;
    r.reading = parsed.reading;
    const Uncertainty& u = report.uncertainty;
    const Ruler lead = options.uncertaintyRule == UncertaintyRule::Linear ? u.linear : u.quadrature;
    const UncertainForms forms = uncertainForms(toRational(ev.value), report.bound + lead);
    r.uncertaintyShown = Digits{forms.shown.negative, forms.shown.digits, forms.shown.exponent10};
    r.concise = forms.concise;
    r.plusMinus = forms.plusMinus;
    if (!u.sources.empty()) {
        for (const UncertainSource& source : u.sources)
            r.uncertainInputs.push_back({sourceName(text, parsed.ast.nodes[static_cast<std::size_t>(source.node)].span),
                                         formatScientific(source.uncertainty), formatScientific(source.sensitivity),
                                         formatScientific(source.contribution)});
        r.uncertaintyLinear = formatScientific(u.linear);
        r.uncertaintyQuadrature = formatScientific(u.quadrature);
    }
    r.unitKnown = report.unit.kind == UnitState::Kind::Known;
    if (r.unitKnown) r.unit = unitName(report.unit.dimension);
    r.warnings.insert(r.warnings.end(), report.unitNotes.begin(), report.unitNotes.end());
    r.firstOrderChecked = u.checked;
    r.firstOrderReliable = u.reliable;
    if (u.checked) r.firstOrderObserved = formatScientific(u.observed);
    if (u.checked && !u.reliable) {
        const std::string message = r.firstOrderObserved == "inf"
                                        ? "first order unreliable: an input shifted by its limit leaves a function's domain"
                                        : "first order unreliable: at the corners the result moved by " + r.firstOrderObserved;
        r.warnings.push_back({WarningCode::FirstOrderUnreliable, message, 0, text.size()});
    }
    if (parsed.target) {
        const Rational value = toRational(ev.value);
        const TargetInput in{parsed, options, value, report};
        if (auto e = findTarget(parsed.target->name)->apply(in, r)) {
            Result failed;
            failed.type = r.type;
            failed.error = std::move(e);
            return failed;
        }
    }
    return r;
}

Result evaluateWithNames(std::string_view text, const Options& options, const Names& names) {
    const Parsed parsed = parse(text, options, names);
    Result r;
    r.type = options.type;
    if (parsed.error) {
        r.error = parsed.error;
        return r;
    }
    if (parsed.commentOnly) {
        r.comment = parsed.comment;
        r.commentOnly = true;
        return r;
    }
    if (parsed.target && !findTarget(parsed.target->name)) {
        const TargetText& t = *parsed.target;
        r.error = Error{ErrorCode::UnknownTarget, "Unknown conversion '" + t.name + "'", t.span.begin, t.span.begin + t.name.size()};
        return r;
    }
    if (options.type == NumberType::Exact) {
        if (auto e = checkExact(parsed.ast)) {
            r.error = std::move(e);
            return r;
        }
    }
    switch (options.type) {  // the one place where a runtime type meets a compile-time T
    case NumberType::Float: return build<float>(parsed, options, text);
    case NumberType::Double: return build<double>(parsed, options, text);
    case NumberType::LongDouble: return build<long double>(parsed, options, text);
    case NumberType::Exact: return build<Rational>(parsed, options, text);
    case NumberType::Binary128: return build<Binary128>(parsed, options, text);
    case NumberType::Binary256: return build<Binary256>(parsed, options, text);
    case NumberType::Binary512: return build<Binary512>(parsed, options, text);
    }
    return r;
}

// The session's names: its variables, then Ans and M.
Names namesOf(const Session::Variables& variables, const std::string& answer, const std::string& memory) {
    Names names(variables.begin(), variables.end());
    if (!answer.empty()) names["Ans"] = answer;
    if (!memory.empty()) names["M"] = memory;
    return names;
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

std::vector<TargetDescription> conversionTargets() {
    std::vector<TargetDescription> list;
    for (const Target& t : targets()) list.push_back({std::string(t.name), std::string(t.summary)});
    return list;
}

std::vector<FunctionDescription> functions() {
    std::vector<FunctionDescription> list;
    for (int i = 0; i < functionCount; ++i) {
        const FunctionInfo& info = functionInfo(static_cast<FunctionId>(i));
        if (info.name.empty() || info.id == FunctionId::LogBase) continue;  // log covers both arities
        list.push_back({std::string(info.name), info.minArgs, info.id == FunctionId::Log10 ? 2 : info.maxArgs, info.exact});
        list.back().aliases = otherSpellings(info.id);
    }
    for (const char* name : {"mean", "varp", "stdevp"}) list.push_back({name, 1, -1, true});
    for (const char* name : {"var", "stdev"}) list.push_back({name, 2, -1, true});
    list.push_back({"mod", 2, 2, true});  // the word exists under every convention
    for (const char* name : {"sum", "product"}) list.push_back({name, 3, 4, true});
    for (const char* name : {"floatBits", "floatParts", "floatValue", "floatError"}) list.push_back({name, 1, 2, true});  // display forms
    for (const auto& [name, exact] : {std::pair<const char*, bool>{"log2", false}, {"exp2", true}, {"exp10", true}, {"sq", true}, {"sqrtpi", false},
                                     {"sec", false}, {"csc", false}, {"cot", false}, {"sech", false}, {"coth", false},
                                     {"asec", false}, {"acsc", false}, {"asech", false}, {"acsch", false}, {"acoth", false}, {"ceil", true}, {"frac", true}})
        list.push_back({name, 1, 1, exact});
    for (FunctionDescription& f : list)
        if (const FunctionText* text = functionText(f.name)) {
            f.title = text->title;
            f.description = text->description;
            f.arguments = text->arguments;
            f.example = text->example;
            f.category = text->category;
        }
    for (FunctionDescription& f : list)
        for (std::string& alias : otherSpellings(std::string_view(f.name))) f.aliases.push_back(std::move(alias));  // a lowering's
    return list;
}

std::vector<std::string> functionCategories() { return categories(); }

std::vector<ConstantDescription> constants() {
    std::vector<ConstantDescription> list;
    for (const auto& [name, title] : {std::pair<const char*, const char*>{"pi", "pi"}, {"e", "Euler's number"}, {"tau", "tau, 2π"},
                                      {"sqrt2", "square root of 2"}, {"phi", "golden ratio"}, {"egamma", "Euler–Mascheroni constant"},
                                      {"catalan", "Catalan's constant"}, {"apery", "Apéry's constant, ζ(3)"},
                                      {"plastic", "plastic ratio"}, {"omega", "omega constant, W(1)"}})
        list.push_back({name, title, "mathematical", "", "", "", "", false, ""});
    for (const NumberName& n : numberNames)
        list.push_back({std::string(n.name), std::string(n.title), "number name", std::string(n.literal), "", "", "", true, ""});
    for (const PhysicalConstant& c : physicalConstants) {
        ConstantDescription d{std::string(c.name), std::string(c.quantity), "physical", std::string(c.nistValue), "", "", "", false, ""};
        if (const std::size_t pm = c.definition.find("+/-"); pm != std::string_view::npos) {
            d.uncertainty = std::string(c.definition.substr(pm + 3));
            d.limit = formatScientific(fromRational<Ruler>(Rational(3) * toRational(*parseDecimal(d.uncertainty))));
        }
        if (c.coherent) {
            Dimension dimension;
            for (std::size_t i = 0; i < dimension.exponents.size(); ++i) dimension.exponents[i] = c.dimension[i];
            d.unit = unitName(dimension);
        } else {
            d.unit = std::string(c.unit);
        }
        const Parsed p = parse(c.name, Options{});
        d.exact = !p.error && !checkExact(p.ast);
        d.group = std::string(c.group);
        list.push_back(std::move(d));
    }
    return list;
}

Result evaluate(std::string_view expression, const Options& options) {
    return evaluateWithNames(expression, options, {});
}

Result Session::evaluate(std::string_view expression, const Options& options) {
    Result r = evaluateWithNames(expression, options, namesOf(variables_, answer_, memory_));
    if (!r.error) {
        if (!r.commentOnly) answer_ = r.expression;  // a note changes nothing but the history
        if (!r.assigned.empty()) variables_[r.assigned] = r.expression;
        history_.push_back({std::string(expression), r});
    }
    return r;
}

bool Session::forget(std::string_view name) {
    const auto found = variables_.find(name);
    if (found == variables_.end()) return false;
    variables_.erase(found);
    return true;
}

Result Session::preview(std::string_view expression, const Options& options) const {
    return evaluateWithNames(expression, options, namesOf(variables_, answer_, memory_));
}

bool Session::memoryAdd() {
    if (answer_.empty()) return false;
    memory_ = memory_.empty() ? answer_ : memory_ + "+(" + answer_ + ")";
    return true;
}

bool Session::memorySubtract() {
    if (answer_.empty()) return false;
    memory_ += "-(" + answer_ + ")";
    return true;
}

bool Session::memoryStore() {
    if (answer_.empty()) return false;
    memory_ = answer_;
    return true;
}

void Session::memoryClear() { memory_.clear(); }

}  // namespace calculate_core
