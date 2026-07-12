#pragma once

#include "tgfx2/tgfx2_api.h"

#include <string>
#include <vector>

namespace tgfx::internal {

struct TGFX2_TYPE_API ProcessResult {
    int exit_code = 127;
    std::string start_error;
    std::string output;
};

TGFX2_API ProcessResult run_process(
    const std::vector<std::string>& args,
    bool capture_output = false
);

} // namespace tgfx::internal
