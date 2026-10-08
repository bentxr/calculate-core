#pragma once

#include "ast.hpp"
#include "numbers.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
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

// One uncertain quantity: a value typed with ±, a measured constant, a number read with its precision.
struct UncertainSource {
    std::vector<int> nodes;  // its Uncertain nodes, in tree order (the same key)
    int node = 0;            // nodes.front(): its span names it
    Ruler uncertainty = 0;   // u, of its first node
    Ruler sensitivity = 0;   // |sum of the signed adjoints of its nodes|: |d result / d quantity|
    int direction = 1;       // the sign of that sum (+1 when it is 0): where the worst case lies
    Ruler contribution = 0;  // sensitivity * u (0 when u is 0, even with an infinite sensitivity)
};

struct Uncertainty {
    std::vector<UncertainSource> sources;  // largest contribution first; ties keep tree order
    Ruler linear = 0;                      // worst case: the sum of the contributions
    Ruler quadrature = 0;                  // statistical: the square root of the sum of their squares
};

// The nodes the result depends on, without going into an Uncertain node's uncertainty argument.
inline std::vector<bool> reachable(const Ast& ast) {
    std::vector<bool> on(ast.nodes.size(), false);
    if (ast.nodes.empty()) return on;
    on[static_cast<std::size_t>(ast.root())] = true;
    for (std::size_t i = ast.nodes.size(); i-- > 0;) {
        if (!on[i]) continue;
        const Node& node = ast.nodes[i];
        for (std::size_t k = 0; k < node.args.size(); ++k)
            if (!(node.function == FunctionId::Uncertain && k == 1)) on[static_cast<std::size_t>(node.args[k])] = true;
    }
    return on;
}

// The quantities (one per key: a constant's name, or each typed ± on its own), how much the result moves with each,
// and the two combinations.
inline Uncertainty combine(const Ast& ast, const std::vector<Ruler>& signedAdjoints, const std::vector<Ruler>& uncertainties) {
    using std::abs;
    using std::sqrt;
    Uncertainty u;
    const std::vector<bool> on = reachable(ast);
    std::map<std::string, std::size_t> position;
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Node& node = ast.nodes[i];
        if (!on[i] || node.function != FunctionId::Uncertain) continue;
        const std::string key = node.text.empty() ? "#" + std::to_string(i) : node.text;
        const auto [at, added] = position.emplace(key, u.sources.size());
        if (added) u.sources.emplace_back();
        u.sources[at->second].nodes.push_back(static_cast<int>(i));
    }
    Ruler squares = 0;
    for (UncertainSource& s : u.sources) {
        s.node = s.nodes.front();
        s.uncertainty = uncertainties[static_cast<std::size_t>(s.node)];
        Ruler slope = 0;
        for (const int n : s.nodes) slope += signedAdjoints[static_cast<std::size_t>(n)];
        if (slope != slope) slope = std::numeric_limits<Ruler>::infinity();  // two infinite slopes of opposite sign
        s.direction = slope < 0 ? -1 : 1;
        s.sensitivity = abs(slope);
        s.contribution = s.uncertainty == 0 ? Ruler(0) : Ruler(s.sensitivity * s.uncertainty);
        u.linear += s.contribution;
        squares += s.contribution * s.contribution;
    }
    u.quadrature = sqrt(squares);
    std::stable_sort(u.sources.begin(), u.sources.end(),
                     [](const UncertainSource& a, const UncertainSource& b) { return a.contribution > b.contribution; });
    return u;
}

}  // namespace calculate_core::detail
