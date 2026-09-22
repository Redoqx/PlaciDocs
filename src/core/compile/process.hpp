#pragma once

#include <string>
#include <vector>

#include "core/util.hpp"

namespace placi {

struct ProcessResult {
    int exit_code = -1;
    std::string output;  // stdout and stderr, interleaved
};

// Runs `exe` with `args` (no shell involved) and captures its output.
ProcessResult run_process(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd = {});

// Directory containing the running executable.
fs::path executable_dir();

}  // namespace placi
