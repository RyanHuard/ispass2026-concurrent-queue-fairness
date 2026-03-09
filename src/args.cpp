#include "args.hpp"

#include <sstream>
#include <string>
#include <iostream>
#include <cstdlib>

namespace bench {
    static std::vector<std::string> split_csv(const std::string& s) {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (!tok.empty()) out.push_back(tok);
        }
        return out; 
    }

    static void usage(const char* prog) {
        std::cerr << "Usage: " << prog
                  << " [--queue=ms|fc|lcrq|lprq|faa|all] [--trials=N] [--ops=N] [--threads=N]\n";
    }   

    Options parse_args(int argc, char *argv[]) {
        Options opts;

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];

            if (arg.rfind("--queue=", 0) == 0) {
                std::string val = arg.substr(8);
                if (val == "all") opts.queues = {"ms", "fc", "lprq", "faa"};
                else opts.queues = split_csv(val);
            } 
            else if (arg.rfind("--trials=", 0) == 0) {
                try { opts.trials = std::stoi(arg.substr(9)); }
                catch (...) { usage(argv[0]); std::exit(1); }
            } 
            else if (arg.rfind("--ops=", 0) == 0) {
                try { opts.ops = std::stoi(arg.substr(6)); }
                catch (...) { usage(argv[0]); std::exit(1); }
            } 
            else if (arg.rfind("--threads=", 0) == 0) {
                try { opts.max_threads = std::stoi(arg.substr(10)); }
                catch (...) { usage(argv[0]); std::exit(1); }
            }
            else if (arg == "--help" || arg == "-h") {
                usage(argv[0]); std::exit(0);
            } 
            else {
                std::cerr << "Unknown arg: " << arg << "\n";
                usage(argv[0]); std::exit(1);
            }
        }
        if (opts.queues.empty()) opts.queues = {"ms", "fc", "lprq", "faa"};
        return opts;
    }
}

