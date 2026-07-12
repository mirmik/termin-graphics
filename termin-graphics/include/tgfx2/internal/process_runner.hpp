#pragma once

#include <string>
#include <vector>

namespace tgfx::internal {

struct ProcessResult {
    int exit_code = 127;
    std::string start_error;
    std::string output;
};

ProcessResult run_process(
    const std::vector<std::string>& args,
    bool capture_output = false
);

} // namespace tgfx::internal
