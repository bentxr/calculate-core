#include "targets.hpp"

namespace calculate_core::detail {

namespace {

// The stored value as the exact fraction it is: what the number type really holds.
std::optional<Error> fraction(const TargetInput& in, Result& result) {
    const TargetText& target = *in.parsed.target;
    if (!target.argument.empty())
        return Error{ErrorCode::UnexpectedToken, "fraction takes nothing after it", target.span.begin, target.span.end};
    std::string text = (in.value < 0 ? "-" : "") + Integer(abs(numerator(in.value))).str();
    if (denominator(in.value) != 1) text += "/" + denominator(in.value).str();
    result.conversion = Conversion{"fraction", text};
    return std::nullopt;
}

}  // namespace

const std::vector<Target>& targets() {
    static const std::vector<Target> list{
        {"fraction", "the exact fraction the result is stored as", fraction},
    };
    return list;
}

const Target* findTarget(std::string_view name) {
    for (const Target& t : targets())
        if (t.name == name) return &t;
    return nullptr;
}

}  // namespace calculate_core::detail
