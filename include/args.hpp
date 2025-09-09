#pragma once

#include <string>
#include <vector>

namespace bench {

struct Options {
    std::vector<std::string> queues;
    int trials = 10;
    int ops = 10'000;
    int max_threads = 16;
};

Options parse_args(int argc, char *argv[]);

}