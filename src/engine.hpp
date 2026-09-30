#pragma once

#include "ast.hpp"
#include "functions.hpp"
#include "numbers.hpp"

#include <calculate-core/calculate-core.hpp>

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace calculate_core::detail {

// Node values of one evaluation in T.
template <class T>
struct Forward {
    std::optional<Error> error;
    std::vector<T> values;
    std::vector<int> roundings;  // per node, for Counted functions
};

namespace impl {

inline Error nodeError(const Node& node, ErrorCode code, std::string message) {
    return Error{code, std::move(message), node.span.begin, node.span.end};
}

}  // namespace impl

// Evaluates every node in T. The first error stops the pass and carries the failing node's span.
template <class T>
Forward<T> forward(const Ast& ast, const std::atomic<bool>* cancel = nullptr) {
    Forward<T> fw;
    fw.values.resize(ast.nodes.size());
    fw.roundings.assign(ast.nodes.size(), 0);
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Node& node = ast.nodes[i];
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            fw.error = impl::nodeError(node, ErrorCode::Cancelled, errorMessage(ErrorCode::Cancelled, ""));
            return fw;
        }
        if (node.function == FunctionId::Literal) {
            // An exact literal beyond 10^±1000000 cannot be materialized in reasonable time or memory.
            const auto literal = parseDecimal(node.text);
            const bool outOfRange =
                !literal || (isExact<T> && (literal->exponent10 > 1000000 || literal->exponent10 < -1000000));
            if (!outOfRange) fw.values[i] = decimalTo<T>(*literal);
            if (outOfRange || !isFinite(fw.values[i])) {
                fw.error = impl::nodeError(node, ErrorCode::LiteralOutOfRange,
                                           errorMessage(ErrorCode::LiteralOutOfRange, ""));
                return fw;
            }
            continue;
        }
        std::vector<T> args;
        for (const int a : node.args) args.push_back(fw.values[a]);
        const Applied<T> r = applyFunction<T>(node.function, args, cancel);
        if (r.error) {
            fw.error = impl::nodeError(node, *r.error, errorMessage(*r.error, functionInfo(node.function).name));
            return fw;
        }
        fw.values[i] = r.value;
        fw.roundings[i] = r.roundings;
    }
    return fw;
}

}  // namespace calculate_core::detail
