#pragma once

#include "ast.hpp"

#include <string>
#include <vector>

namespace test {

using calculate_core::detail::Ast;
using calculate_core::detail::FunctionId;
using calculate_core::detail::Node;

// Builds post-order ASTs by hand: every call appends one node, so the last one is the root.
class AstBuilder {
public:
    struct Handle {
        AstBuilder* builder;
        int index;
    };

    Handle literal(std::string text) {
        Node node;
        node.text = std::move(text);
        return push(std::move(node));
    }

    Handle apply(FunctionId id, std::vector<Handle> args) {
        Node node;
        node.function = id;
        for (const Handle& h : args) node.args.push_back(h.index);
        return push(std::move(node));
    }

    Handle constant(FunctionId id) { return apply(id, {}); }

    const Ast& ast() const { return ast_; }

private:
    Handle push(Node node) {
        ast_.nodes.push_back(std::move(node));
        return {this, static_cast<int>(ast_.nodes.size()) - 1};
    }

    Ast ast_;
};

inline AstBuilder::Handle operator+(AstBuilder::Handle a, AstBuilder::Handle b) { return a.builder->apply(FunctionId::Add, {a, b}); }
inline AstBuilder::Handle operator-(AstBuilder::Handle a, AstBuilder::Handle b) { return a.builder->apply(FunctionId::Subtract, {a, b}); }
inline AstBuilder::Handle operator*(AstBuilder::Handle a, AstBuilder::Handle b) { return a.builder->apply(FunctionId::Multiply, {a, b}); }
inline AstBuilder::Handle operator/(AstBuilder::Handle a, AstBuilder::Handle b) { return a.builder->apply(FunctionId::Divide, {a, b}); }
inline AstBuilder::Handle operator-(AstBuilder::Handle a) { return a.builder->apply(FunctionId::Negate, {a}); }

// x^n (n >= 1) as a chain of multiplications that all share x.
inline AstBuilder::Handle powi(AstBuilder::Handle x, int n) {
    AstBuilder::Handle result = x;
    for (int k = 1; k < n; ++k) result = result * x;
    return result;
}

}  // namespace test
