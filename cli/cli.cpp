#include "cli.hpp"

#include <istream>
#include <optional>
#include <ostream>

namespace calc {

using namespace calculate_core;

namespace {

struct TypeName {
    const char* option;
    NumberType type;
};

constexpr TypeName typeNames[] = {
    {"float", NumberType::Float},
    {"double", NumberType::Double},
    {"long-double", NumberType::LongDouble},
    {"exact", NumberType::Exact},
    {"binary128", NumberType::Binary128},
    {"binary256", NumberType::Binary256},
    {"binary512", NumberType::Binary512},
};

std::string usage() {
    return "Usage: calc [options] [expression ...]\n"
           "Evaluates each expression (or each line of standard input) and shows its error.\n"
           "\n"
           "Options:\n"
           "  --type <t>         float, double (default), long-double, exact,\n"
           "                     binary128, binary256 or binary512\n"
           "  --angle <u>        rad (default), deg or grad\n"
           "  --json             one JSON object per expression\n"
           "  --color <when>     auto (default), always or never\n"
           "  --allow-uncertain  let discrete functions take arguments that carry error\n"
           "  --list-types       describe the number types of this build\n"
           "  --help, --version\n"
           "\n"
           "Lines M+, M- and MC add Ans to, subtract it from, or clear the memory M.\n";
}

struct Settings {
    Options options;
    bool json = false;
    bool color = false;
};

}  // namespace

int run(const std::vector<std::string>& args, std::istream& /*in*/, std::ostream& out, std::ostream& err,
        bool terminal) {
    Settings s;
    std::string colorWhen = "auto";
    std::vector<std::string> expressions;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const auto value = [&]() -> std::optional<std::string> {
            if (i + 1 < args.size()) return args[++i];
            err << "calc: " << a << " needs a value\n";
            return std::nullopt;
        };
        if (a == "--help") {
            out << usage();
            return 0;
        }
        if (a == "--version") {
            out << "calc " << CALCULATE_VERSION << "\n";
            return 0;
        }
        if (a == "--json") {
            s.json = true;
            continue;
        }
        if (a == "--allow-uncertain") {
            s.options.allowUncertainDiscreteArguments = true;
            continue;
        }
        if (a == "--type") {
            const auto v = value();
            if (!v) return 2;
            bool found = false;
            for (const TypeName& t : typeNames)
                if (*v == t.option) {
                    s.options.type = t.type;
                    found = true;
                }
            if (!found) {
                err << "calc: unknown type '" << *v << "' (see --list-types)\n";
                return 2;
            }
            continue;
        }
        if (a == "--angle") {
            const auto v = value();
            if (!v) return 2;
            if (*v == "rad") s.options.angle = AngleUnit::Radians;
            else if (*v == "deg") s.options.angle = AngleUnit::Degrees;
            else if (*v == "grad") s.options.angle = AngleUnit::Gradians;
            else {
                err << "calc: unknown angle unit '" << *v << "' (rad, deg or grad)\n";
                return 2;
            }
            continue;
        }
        if (a == "--color") {
            const auto v = value();
            if (!v) return 2;
            if (*v != "auto" && *v != "always" && *v != "never") {
                err << "calc: --color takes auto, always or never\n";
                return 2;
            }
            colorWhen = *v;
            continue;
        }
        if (a.size() > 2 && a.compare(0, 2, "--") == 0) {
            err << "calc: unknown option '" << a << "'\nTry 'calc --help'.\n";
            return 2;
        }
        expressions.push_back(a);
    }
    s.color = !s.json && (colorWhen == "always" || (colorWhen == "auto" && terminal));
    return 0;
}

}  // namespace calc
