#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace calculate_core::detail {

enum class FunctionId {
    Literal,
    Pi, E,
    Add, Subtract, Multiply, Divide, Negate, Power, Percent, Square, Cube, Factorial,
    Sqrt, Cbrt, Root, Exp, Ln, Log10, LogBase,
    Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, Asinh, Acosh, Atanh,
    Abs, Mod, Gcd, Lcm, Ncr, Npr, Median
};

inline constexpr int functionCount = static_cast<int>(FunctionId::Median) + 1;

struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct Node {
    FunctionId function = FunctionId::Literal;
    std::string text;       // the literal's source text (Literal only)
    std::vector<int> args;  // indices of earlier nodes
    Span span;
};

// Post-order arena: every node's arguments come before it; the root is the last node.
// A node may be the argument of several nodes (a DAG).
struct Ast {
    std::vector<Node> nodes;
    int root() const { return static_cast<int>(nodes.size()) - 1; }
};

// True when the tree is non-empty and every argument index points to an earlier node.
inline bool isPostOrder(const Ast& ast) {
    for (int i = 0; i < static_cast<int>(ast.nodes.size()); ++i)
        for (const int a : ast.nodes[i].args)
            if (a < 0 || a >= i) return false;
    return !ast.nodes.empty();
}

}  // namespace calculate_core::detail
