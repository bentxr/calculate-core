#include "functions.hpp"
#include "test_support.hpp"

#include <set>

using namespace calculate_core;
using namespace calculate_core::detail;

TEST(FunctionInfo, EveryIdHasItsOwnRow) {
    for (int i = 0; i < functionCount; ++i)
        EXPECT_EQ(static_cast<int>(functionInfo(static_cast<FunctionId>(i)).id), i);
}

TEST(FunctionInfo, NamesAreUniqueExceptTheTwoLogarithms) {
    std::multiset<std::string_view> names;
    for (int i = 0; i < functionCount; ++i)
        if (!functionInfo(static_cast<FunctionId>(i)).name.empty())
            names.insert(functionInfo(static_cast<FunctionId>(i)).name);
    for (std::string_view name : names)
        EXPECT_EQ(names.count(name), name == "log" ? 2u : 1u) << name;
}

TEST(FunctionInfo, DiscreteAndExactFlags) {
    for (FunctionId id : {FunctionId::Factorial, FunctionId::Gcd, FunctionId::Lcm, FunctionId::Ncr, FunctionId::Npr})
        EXPECT_TRUE(functionInfo(id).discrete);
    EXPECT_FALSE(functionInfo(FunctionId::Mod).discrete);
    EXPECT_FALSE(functionInfo(FunctionId::Sin).exact);
    EXPECT_FALSE(functionInfo(FunctionId::Pi).exact);
    EXPECT_TRUE(functionInfo(FunctionId::Sqrt).exact);
    EXPECT_TRUE(functionInfo(FunctionId::Divide).exact);
    EXPECT_EQ(functionInfo(FunctionId::Median).maxArgs, -1);
}
