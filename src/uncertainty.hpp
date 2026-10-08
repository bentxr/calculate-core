#pragma once

#include "ast.hpp"
#include "numbers.hpp"

#include <vector>

// The user's uncertainty: values written with one (5 ± 0.2), how they reach the result.
namespace calculate_core::detail {

// Each node's own uncertainty u: |its uncertainty argument| for Uncertain nodes, 0 elsewhere.
template <class T>
std::vector<Ruler> userUncertainties(const Ast& ast, const std::vector<T>& values) {
    using std::abs;
    std::vector<Ruler> u(ast.nodes.size(), Ruler(0));
    for (std::size_t i = 0; i < ast.nodes.size(); ++i)
        if (ast.nodes[i].function == FunctionId::Uncertain) u[i] = abs(exactCast<Ruler>(values[ast.nodes[i].args[1]]));
    return u;
}

}  // namespace calculate_core::detail
