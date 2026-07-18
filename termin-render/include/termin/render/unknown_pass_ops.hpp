#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <inspect/tc_inspect.h>
#include <render/tc_pass.h>
#include <termin/render/render_export.hpp>

namespace termin {

struct RENDER_API UnknownPassStats {
    size_t degraded = 0;
    size_t upgraded = 0;
    size_t skipped = 0;
    size_t failed = 0;
};

struct RENDER_API UnknownPassPreparationHooks {
    std::function<tc_value(void*, const char*)> serialize;
    std::function<tc_pass*()> create_replacement;
};

class RENDER_API UnknownPassDegradationPlan {
public:
    UnknownPassDegradationPlan();
    UnknownPassDegradationPlan(UnknownPassDegradationPlan&&) noexcept;
    UnknownPassDegradationPlan& operator=(UnknownPassDegradationPlan&&) noexcept;
    ~UnknownPassDegradationPlan();

    UnknownPassDegradationPlan(const UnknownPassDegradationPlan&) = delete;
    UnknownPassDegradationPlan& operator=(const UnknownPassDegradationPlan&) = delete;

    size_t size() const;
    bool empty() const;
    bool committed() const;
    bool validate(std::string* error = nullptr) const;
    bool commit(std::string* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    friend bool prepare_passes_to_unknown(
        const std::vector<std::string>&,
        UnknownPassDegradationPlan&,
        std::string*,
        const UnknownPassPreparationHooks&
    );
};

RENDER_API bool prepare_passes_to_unknown(
    const std::vector<std::string>& type_names,
    UnknownPassDegradationPlan& plan,
    std::string* error = nullptr,
    const UnknownPassPreparationHooks& hooks = {}
);

RENDER_API UnknownPassStats degrade_passes_to_unknown(
    const std::vector<std::string>& type_names
);

RENDER_API UnknownPassStats upgrade_unknown_passes(
    const std::vector<std::string>& type_names = {}
);

} // namespace termin
