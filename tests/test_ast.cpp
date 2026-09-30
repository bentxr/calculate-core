#include "ast_builder.hpp"

#include <gtest/gtest.h>

using namespace calculate_core::detail;

TEST(Ast, BuilderProducesPostOrder) {
    test::AstBuilder b;
    const auto one = b.literal("1"), two = b.literal("2"), three = b.literal("3");
    one + two * three;
    const Ast& ast = b.ast();
    ASSERT_EQ(ast.nodes.size(), 5u);
    EXPECT_TRUE(isPostOrder(ast));
    EXPECT_EQ(ast.root(), 4);
    EXPECT_EQ(ast.nodes[4].function, FunctionId::Add);
    EXPECT_EQ(ast.nodes[3].function, FunctionId::Multiply);
    EXPECT_EQ(ast.nodes[4].args, (std::vector<int>{0, 3}));
}

TEST(Ast, SharedNodesMakeADag) {
    test::AstBuilder b;
    const auto x = b.literal("3");
    x * x + x;
    EXPECT_TRUE(isPostOrder(b.ast()));
    EXPECT_EQ(b.ast().nodes.size(), 3u);
}

TEST(Ast, RejectsForwardReferencesAndEmptyTrees) {
    Ast ast;
    EXPECT_FALSE(isPostOrder(ast));
    Node add;
    add.function = FunctionId::Add;
    add.args = {0, 1};
    ast.nodes.push_back(add);
    EXPECT_FALSE(isPostOrder(ast));
}
