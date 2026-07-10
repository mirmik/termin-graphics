#pragma once

#include <string>
#include <vector>

#include <termin/render/render_export.hpp>

namespace termin {

struct RENDER_API UnknownPassStats {
    size_t degraded = 0;
    size_t upgraded = 0;
    size_t skipped = 0;
    size_t failed = 0;
};

RENDER_API UnknownPassStats degrade_passes_to_unknown(
    const std::vector<std::string>& type_names
);

RENDER_API UnknownPassStats upgrade_unknown_passes(
    const std::vector<std::string>& type_names = {}
);

} // namespace termin
