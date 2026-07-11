#include <termin/render/unknown_pass_ops.hpp>

#include <algorithm>
#include <unordered_set>

#include <inspect/tc_inspect.h>
#include <tc_pipeline_registry.h>
#include <tcbase/tc_log.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/unknown_pass.hpp>

namespace termin {
namespace {

constexpr size_t kMaxGraphEntries = 1024;

bool is_unknown_pass(const tc_pass* pass) {
    const char* type_name = pass ? tc_pass_type_name(pass) : nullptr;
    return type_name && std::string(type_name) == "UnknownPass";
}

void* pass_object_ptr(tc_pass* pass) {
    if (!pass) return nullptr;
    return pass->kind == TC_NATIVE_PASS
        ? static_cast<void*>(CxxFramePass::from_tc(pass))
        : pass->body;
}

std::vector<std::string> collect_names(
    tc_pass* pass,
    size_t (*collector)(tc_pass*, const char**, size_t)
) {
    std::vector<const char*> values(kMaxGraphEntries, nullptr);
    const size_t count = collector(pass, values.data(), values.size());
    std::vector<std::string> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (values[i]) result.emplace_back(values[i]);
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> collect_aliases(tc_pass* pass) {
    std::vector<const char*> values(kMaxGraphEntries * 2, nullptr);
    const size_t count = tc_pass_get_inplace_aliases(
        pass,
        values.data(),
        kMaxGraphEntries
    );
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        result.emplace_back(
            values[i * 2] ? values[i * 2] : "",
            values[i * 2 + 1] ? values[i * 2 + 1] : ""
        );
    }
    return result;
}

std::vector<ResourceSpec> collect_specs(tc_pass* pass) {
    std::vector<ResourceSpec> values(kMaxGraphEntries);
    const size_t count = tc_pass_get_resource_specs(
        pass,
        values.data(),
        values.size()
    );
    values.resize(count);
    return values;
}

bool type_selected(
    const std::unordered_set<std::string>& filter,
    const std::string& type_name
) {
    return filter.empty() || filter.count(type_name) != 0;
}

bool degrade_at(
    tc_pipeline_handle pipeline,
    size_t index,
    tc_pass* pass,
    std::string& error
) {
    const char* raw_type = tc_pass_type_name(pass);
    if (!raw_type || !raw_type[0]) {
        error = "pass has no runtime type";
        return false;
    }
    void* object = pass_object_ptr(pass);
    if (!object) {
        error = "pass has no inspect object";
        return false;
    }

    ensure_unknown_pass_registered();
    tc_pass* unknown_tc = tc_pass_registry_create("UnknownPass");
    if (!unknown_tc) {
        error = "failed to create UnknownPass";
        return false;
    }
    auto* unknown = dynamic_cast<UnknownPass*>(CxxFramePass::from_tc(unknown_tc));
    if (!unknown) {
        tc_pass_delete_unowned(unknown_tc);
        error = "UnknownPass factory returned incompatible object";
        return false;
    }

    unknown->original_type = raw_type;
    tc_value_free(&unknown->original_data);
    unknown->original_data = tc_inspect_serialize(object, raw_type);
    unknown->original_reads = collect_names(pass, tc_pass_get_reads);
    unknown->original_writes = collect_names(pass, tc_pass_get_writes);
    unknown->original_inplace_aliases = collect_aliases(pass);
    unknown->original_resource_specs = collect_specs(pass);
    unknown->original_internal_symbols = collect_names(pass, tc_pass_get_internal_symbols);

    tc_pass_set_name(unknown_tc, pass->pass_name);
    tc_pass_set_enabled(unknown_tc, pass->enabled);
    tc_pass_set_passthrough(unknown_tc, pass->passthrough);
    tc_pass_set_viewport_name(unknown_tc, pass->viewport_name);
    unknown->set_debug_internal_point(
        pass->debug_internal_symbol ? pass->debug_internal_symbol : ""
    );

    if (!tc_pipeline_replace_pass_at(pipeline, index, unknown_tc, unknown_tc->deleter)) {
        tc_pass_delete_unowned(unknown_tc);
        error = "failed to replace pass in pipeline";
        return false;
    }
    return true;
}

bool upgrade_at(
    tc_pipeline_handle pipeline,
    size_t index,
    tc_pass* pass,
    std::string& error
) {
    auto* unknown = dynamic_cast<UnknownPass*>(CxxFramePass::from_tc(pass));
    if (!unknown || unknown->original_type.empty()) {
        error = "UnknownPass has no original type";
        return false;
    }
    if (!tc_pass_registry_has(unknown->original_type.c_str())) {
        error = "original pass type is not registered: " + unknown->original_type;
        return false;
    }

    tc_pass* restored = tc_pass_registry_create(unknown->original_type.c_str());
    if (!restored) {
        error = "failed to create pass: " + unknown->original_type;
        return false;
    }
    void* object = pass_object_ptr(restored);
    if (!object) {
        tc_pass_delete_unowned(restored);
        error = "restored pass has no inspect object";
        return false;
    }

    const tc_inspect_apply_result applied = tc_inspect_deserialize_checked(
        object,
        unknown->original_type.c_str(),
        &unknown->original_data,
        nullptr
    );
    if (applied.status != TC_INSPECT_APPLY_OK) {
        const std::string field = applied.field_path
            ? " at field '" + std::string(applied.field_path) + "'"
            : std::string();
        tc_pass_delete_unowned(restored);
        error = "failed to restore pass payload" + field;
        return false;
    }

    tc_pass_set_name(restored, pass->pass_name);
    tc_pass_set_enabled(restored, pass->enabled);
    tc_pass_set_passthrough(restored, pass->passthrough);
    tc_pass_set_viewport_name(restored, pass->viewport_name);
    if (auto* cxx = CxxFramePass::from_tc(restored)) {
        cxx->set_debug_internal_point(
            pass->debug_internal_symbol ? pass->debug_internal_symbol : ""
        );
    }

    if (!tc_pipeline_replace_pass_at(pipeline, index, restored, restored->deleter)) {
        tc_pass_delete_unowned(restored);
        error = "failed to replace UnknownPass in pipeline";
        return false;
    }
    return true;
}

} // namespace

UnknownPassStats degrade_passes_to_unknown(
    const std::vector<std::string>& type_names
) {
    UnknownPassStats stats;
    const std::unordered_set<std::string> filter(type_names.begin(), type_names.end());
    const size_t pipeline_count = tc_pipeline_registry_count();
    for (size_t pipeline_index = 0; pipeline_index < pipeline_count; ++pipeline_index) {
        const tc_pipeline_handle pipeline = tc_pipeline_registry_get_at(pipeline_index);
        const size_t pass_count = tc_pipeline_pass_count(pipeline);
        for (size_t index = 0; index < pass_count; ++index) {
            tc_pass* pass = tc_pipeline_get_pass_at(pipeline, index);
            if (!pass || is_unknown_pass(pass)) continue;
            const std::string type_name = tc_pass_type_name(pass);
            if (!type_selected(filter, type_name)) continue;

            std::string error;
            if (degrade_at(pipeline, index, pass, error)) {
                ++stats.degraded;
            } else {
                ++stats.failed;
                tc::Log::error(
                    "[UnknownPass] Failed to degrade pass '%s' of type '%s': %s",
                    pass->pass_name ? pass->pass_name : "<unnamed>",
                    type_name.c_str(),
                    error.c_str()
                );
            }
        }
    }
    return stats;
}

UnknownPassStats upgrade_unknown_passes(
    const std::vector<std::string>& type_names
) {
    UnknownPassStats stats;
    const std::unordered_set<std::string> filter(type_names.begin(), type_names.end());
    const size_t pipeline_count = tc_pipeline_registry_count();
    for (size_t pipeline_index = 0; pipeline_index < pipeline_count; ++pipeline_index) {
        const tc_pipeline_handle pipeline = tc_pipeline_registry_get_at(pipeline_index);
        const size_t pass_count = tc_pipeline_pass_count(pipeline);
        for (size_t index = 0; index < pass_count; ++index) {
            tc_pass* pass = tc_pipeline_get_pass_at(pipeline, index);
            if (!is_unknown_pass(pass)) continue;
            auto* unknown = dynamic_cast<UnknownPass*>(CxxFramePass::from_tc(pass));
            if (!unknown || !type_selected(filter, unknown->original_type)) {
                ++stats.skipped;
                continue;
            }
            if (!tc_pass_registry_has(unknown->original_type.c_str())) {
                ++stats.skipped;
                continue;
            }

            std::string error;
            if (upgrade_at(pipeline, index, pass, error)) {
                ++stats.upgraded;
            } else {
                ++stats.failed;
                tc::Log::error(
                    "[UnknownPass] Failed to upgrade pass '%s': %s",
                    pass->pass_name ? pass->pass_name : "<unnamed>",
                    error.c_str()
                );
            }
        }
    }
    return stats;
}

} // namespace termin
