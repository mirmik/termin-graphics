#pragma once

#include <string>
#include <utility>
#include <vector>

#include <termin/render/frame_pass.hpp>

namespace termin {

class RENDER_API UnknownPass final : public CxxFramePass {
public:
    std::string original_type;
    tc_value original_data = tc_value_dict_new();
    std::vector<std::string> original_reads;
    std::vector<std::string> original_writes;
    std::vector<std::pair<std::string, std::string>> original_inplace_aliases;
    std::vector<ResourceSpec> original_resource_specs;
    std::vector<std::string> original_internal_symbols;

    UnknownPass();
    ~UnknownPass() override;

    std::set<const char*> compute_reads() const override;
    std::set<const char*> compute_writes() const override;
    std::vector<std::pair<std::string, std::string>> get_inplace_aliases() const override;
    std::vector<ResourceSpec> get_resource_specs() const override;
    std::vector<std::string> get_internal_symbols() const override;
};

RENDER_API void ensure_unknown_pass_registered();

} // namespace termin
