#pragma once

#include <calculate-core/calculate-core.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace calculate_core::detail {

// The English texts that describe a function of the language (see FunctionDescription).
struct FunctionText {
    std::string_view name, title, description, example, category;
    std::vector<ArgumentDescription> arguments;
};

// The texts of the function with this name; nullptr when it has none.
const FunctionText* functionText(std::string_view name);

// The categories of functionCategories(), in display order.
std::vector<std::string> categories();

}  // namespace calculate_core::detail
