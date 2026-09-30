#include "ast_builder.hpp"
#include "engine.hpp"
#include "test_support.hpp"

using namespace calculate_core;
using namespace calculate_core::detail;
using test::AstBuilder;

namespace {

Ast sum(const char* a, const char* b) {
    AstBuilder builder;
    builder.literal(a) + builder.literal(b);
    return builder.ast();
}

}  // namespace

TEST(Forward, EvaluatesEveryNode) {
    AstBuilder b;
    b.literal("1") + b.literal("2") * b.literal("3");
    const Forward<double> fw = forward<double>(b.ast());
    ASSERT_FALSE(fw.error);
    EXPECT_EQ(fw.values.back(), 7.0);
    EXPECT_EQ(fw.values.size(), 5u);
}

TEST(Forward, ErrorsCarryTheNodeSpan) {
    AstBuilder b;
    b.literal("1") / b.literal("0");
    Ast ast = b.ast();
    ast.nodes.back().span = {2, 3};
    const Forward<double> fw = forward<double>(ast);
    ASSERT_TRUE(fw.error);
    EXPECT_EQ(fw.error->code, ErrorCode::DivisionByZero);
    EXPECT_EQ(fw.error->begin, 2u);
    EXPECT_EQ(fw.error->end, 3u);
    EXPECT_FALSE(fw.error->message.empty());
}

TEST(Forward, LiteralOutOfRange) {
    AstBuilder b;
    b.literal("1e400");
    const Forward<double> fw = forward<double>(b.ast());
    ASSERT_TRUE(fw.error);
    EXPECT_EQ(fw.error->code, ErrorCode::LiteralOutOfRange);
    EXPECT_FALSE(forward<Binary128>(b.ast()).error);  // 1e400 fits in binary128
}

TEST(Forward, ExactLiteralsBeyondTheMaterializationLimitAreOutOfRange) {
    AstBuilder b;
    b.literal("1e1000001");
    const Forward<Rational> fw = forward<Rational>(b.ast());
    ASSERT_TRUE(fw.error);
    EXPECT_EQ(fw.error->code, ErrorCode::LiteralOutOfRange);
}

TEST(Forward, Cancellation) {
    std::atomic<bool> cancel{true};
    const Forward<double> fw = forward<double>(sum("1", "2"), &cancel);
    ASSERT_TRUE(fw.error);
    EXPECT_EQ(fw.error->code, ErrorCode::Cancelled);
}

TEST(LocalErrors, LiteralsAndConstantsCarryTheirRepresentationError) {
    AstBuilder b;
    const auto tenth = b.literal("0.1");
    const auto pi = b.constant(FunctionId::Pi);
    tenth * pi;
    const Forward<double> fw = forward<double>(b.ast());
    const std::vector<Ruler> locals = localErrors<double>(b.ast(), fw);
    EXPECT_EQ(locals[0], fromRational<Ruler>(abs(toRational(0.1) - Rational(1, 10))));
    EXPECT_EQ(locals[1], fromRational<Ruler>(abs(toRational(fw.values[1]) - constantRational(ConstantId::Pi))));
    EXPECT_GT(locals[2], 0);
}
