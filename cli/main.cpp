#include "cli.hpp"

#include <iostream>
#include <unistd.h>

int main(int argc, char** argv) {
    return calc::run(std::vector<std::string>(argv + 1, argv + argc), std::cin, std::cout, std::cerr, isatty(1) != 0);
}
