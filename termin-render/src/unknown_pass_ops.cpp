#include <termin/render/unknown_pass_ops.hpp>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include <inspect/tc_inspect.h>
#include <tc_pipeline_registry.h>
#include <tcbase/tc_log.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/unknown_pass.hpp>

namespace termin {
namespace {

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
    size_t count = collector(pass, nullptr, 0);
    std::vector<const char*> values;
    while (count > 0) {
        values.resize(count);
        const size_t actual = collector(pass, values.data(), count);
        if (actual <= count) {
            values.resize(actual);
            count = actual;
            break;
        }
        count = actual;
    }
    std::vector<std::string> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (values[i]) result.emplace_back(values[i]);
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> collect_aliases(tc_pass* pass) {
    size_t count = tc_pass_get_inplace_aliases(pass, nullptr, 0);
    std::vector<const char*> values;
    while (count > 0) {
        values.resize(count * 2);
        const size_t actual = tc_pass_get_inplace_aliases(
            pass, values.data(), count);
        if (actual <= count) {
            values.resize(actual * 2);
            count = actual;
            break;
        }
        count = actual;
    }
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
    size_t count = tc_pass_get_resource_specs(pass, nullptr, 0);
    std::vector<ResourceSpec> values;
    while (count > 0) {
        values.resize(count);
        const size_t actual = tc_pass_get_resource_specs(
            pass, values.data(), values.size());
        if (actual <= count) {
            values.resize(actual);
            break;
        }
        count = actual;
    }
    return values;
}

bool type_selected(
    const std::unordered_set<std::string>& filter,
    const std::string& type_name
) {
    return filter.empty() || filter.count(type_name) != 0;
}

bool prepare_replacement(
    tc_pass* pass,
    const UnknownPassPreparationHooks& hooks,
    tc_pass** replacement,
    std::string& error
) {
    if (replacement) *replacement = nullptr;
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

    tc_pass* unknown_tc = hooks.create_replacement
        ? hooks.create_replacement()
        : tc_pass_registry_create("UnknownPass");
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
    unknown->original_data = hooks.serialize
        ? hooks.serialize(object, raw_type)
        : tc_inspect_serialize(object, raw_type);
    if (unknown->original_data.type != TC_VALUE_DICT) {
        tc_pass_delete_unowned(unknown_tc);
        error = "pass serialization did not produce a dictionary";
        return false;
    }
    unknown->original_reads = collect_names(pass, tc_pass_get_reads);
    unknown->original_writes = collect_names(pass, tc_pass_get_writes);
    unknown->original_inplace_aliases = collect_aliases(pass);
    unknown->original_resource_specs = collect_specs(pass);
    unknown->original_internal_symbols = collect_names(pass, tc_pass_get_internal_symbols);

    tc_pass_set_name(unknown_tc, pass->pass_name);
    tc_pass_set_enabled(unknown_tc, pass->enabled);
    tc_pass_set_passthrough(unknown_tc, pass->passthrough);
    tc_pass_set_viewport_name(unknown_tc, pass->viewport_name);

    if (!replacement) {
        tc_pass_delete_unowned(unknown_tc);
        error = "replacement output is null";
        return false;
    }
    *replacement = unknown_tc;
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
    if (!tc_pipeline_replace_pass_at(pipeline, index, restored, restored->deleter)) {
        tc_pass_delete_unowned(restored);
        error = "failed to replace UnknownPass in pipeline";
        return false;
    }
    return true;
}

} // namespace

struct UnknownPassDegradationPlan::Impl {
    struct Entry {
        tc_pipeline_handle pipeline = TC_PIPELINE_HANDLE_INVALID;
        size_t index = 0;
        tc_pass* source = nullptr;
        tc_pass* replacement = nullptr;
        tc_pass_deleter source_deleter = nullptr;
        std::string type_name;
    };

    std::vector<Entry> entries;
    std::unordered_map<std::string, size_t> expected_type_counts;
    bool is_committed = false;

    ~Impl() {
        for (Entry& entry : entries) {
            if (entry.replacement) {
                tc_pass_delete_unowned(entry.replacement);
                entry.replacement = nullptr;
            }
        }
    }
};

UnknownPassDegradationPlan::UnknownPassDegradationPlan() :
    _impl(std::make_unique<Impl>()) {}

UnknownPassDegradationPlan::UnknownPassDegradationPlan(
    UnknownPassDegradationPlan&&) noexcept = default;

UnknownPassDegradationPlan& UnknownPassDegradationPlan::operator=(
    UnknownPassDegradationPlan&&) noexcept = default;

UnknownPassDegradationPlan::~UnknownPassDegradationPlan() = default;

size_t UnknownPassDegradationPlan::size() const {
    return _impl ? _impl->entries.size() : 0;
}

bool UnknownPassDegradationPlan::empty() const {
    return size() == 0;
}

bool UnknownPassDegradationPlan::committed() const {
    return _impl && _impl->is_committed;
}

bool UnknownPassDegradationPlan::validate(std::string* error) const {
    if (error) error->clear();
    if (!_impl) {
        if (error) *error = "Pass degradation plan has no state";
        return false;
    }
    if (_impl->is_committed) {
        if (error) *error = "Pass degradation plan is already committed";
        return false;
    }

    for (const Impl::Entry& entry : _impl->entries) {
        if (!tc_pipeline_pool_alive(entry.pipeline) ||
            tc_pipeline_get_pass_at(entry.pipeline, entry.index) != entry.source ||
            !tc_pipeline_handle_eq(entry.source->owner_pipeline, entry.pipeline)) {
            if (error) *error = "Prepared pipeline/pass identity changed before commit";
            return false;
        }
        if (!entry.replacement ||
            tc_pipeline_handle_valid(entry.replacement->owner_pipeline)) {
            if (error) *error = "Prepared UnknownPass is no longer unowned";
            return false;
        }
    }

    for (const auto& [type_name, expected_count] : _impl->expected_type_counts) {
        const size_t live_count = tc_pass_registry_instance_count(type_name.c_str());
        if (live_count != expected_count) {
            if (error) {
                *error = "Pass type '" + type_name + "' has " +
                    std::to_string(live_count) + " live instance(s), but the prepared " +
                    "pipeline batch accounts for " + std::to_string(expected_count);
            }
            return false;
        }
    }
    return true;
}

bool UnknownPassDegradationPlan::commit(std::string* error) {
    if (!validate(error)) return false;

    size_t published = 0;
    for (; published < _impl->entries.size(); ++published) {
        Impl::Entry& entry = _impl->entries[published];
        if (!tc_pipeline_exchange_pass_at_checked(
                entry.pipeline,
                entry.index,
                entry.source,
                entry.replacement,
                entry.replacement->deleter,
                &entry.source_deleter)) {
            for (size_t rollback = published; rollback > 0; --rollback) {
                Impl::Entry& restored = _impl->entries[rollback - 1];
                tc_pass_deleter replacement_deleter = nullptr;
                if (!tc_pipeline_exchange_pass_at_checked(
                        restored.pipeline,
                        restored.index,
                        restored.replacement,
                        restored.source,
                        restored.source_deleter,
                        &replacement_deleter)) {
                    tc::Log::error(
                        "[UnknownPass] Failed to roll back an unexpected publication failure"
                    );
                }
                restored.source_deleter = nullptr;
            }
            if (error) *error = "Prepared pass publication failed after validation";
            tc::Log::error("[UnknownPass] Prepared batch publication failed");
            return false;
        }
    }

    _impl->is_committed = true;
    for (Impl::Entry& entry : _impl->entries) {
        entry.replacement = nullptr;
    }

    // Destruction can execute language/user cleanup. It intentionally happens
    // only after every pipeline slot exposes its prepared placeholder.
    for (Impl::Entry& entry : _impl->entries) {
        tc_pass_destroy(entry.source);
        if (entry.source_deleter) {
            entry.source_deleter(entry.source);
        } else {
            tc::Log::error("[UnknownPass] Detached source pass has no deleter");
        }
        entry.source = nullptr;
        entry.source_deleter = nullptr;
    }
    return true;
}

bool prepare_passes_to_unknown(
    const std::vector<std::string>& type_names,
    UnknownPassDegradationPlan& plan,
    std::string* error,
    const UnknownPassPreparationHooks& hooks
) {
    if (error) error->clear();
    UnknownPassDegradationPlan prepared;
    const std::unordered_set<std::string> filter(
        type_names.begin(), type_names.end());
    for (const std::string& type_name : filter) {
        prepared._impl->expected_type_counts.emplace(type_name, 0);
    }

    try {
        const size_t pipeline_count = tc_pipeline_registry_count();
        for (size_t pipeline_index = 0; pipeline_index < pipeline_count; ++pipeline_index) {
            const tc_pipeline_handle pipeline =
                tc_pipeline_registry_get_at(pipeline_index);
            const size_t pass_count = tc_pipeline_pass_count(pipeline);
            for (size_t index = 0; index < pass_count; ++index) {
                tc_pass* pass = tc_pipeline_get_pass_at(pipeline, index);
                if (!pass || is_unknown_pass(pass)) continue;
                const std::string type_name = tc_pass_type_name(pass);
                if (!type_selected(filter, type_name)) continue;

                tc_pass* replacement = nullptr;
                std::string entry_error;
                if (!prepare_replacement(
                        pass, hooks, &replacement, entry_error)) {
                    if (error) {
                        *error = "Failed to prepare pass '" +
                            std::string(pass->pass_name ? pass->pass_name : "<unnamed>") +
                            "' of type '" + type_name + "': " + entry_error;
                    }
                    tc::Log::error(
                        "[UnknownPass] %s",
                        error ? error->c_str() : entry_error.c_str()
                    );
                    return false;
                }
                prepared._impl->entries.push_back({
                    pipeline,
                    index,
                    pass,
                    replacement,
                    nullptr,
                    type_name
                });
                ++prepared._impl->expected_type_counts[type_name];
            }
        }
    } catch (const std::exception& exception) {
        if (error) {
            *error = "Exception while preparing pass degradation: " +
                std::string(exception.what());
        }
        tc::Log::error(
            "[UnknownPass] Exception while preparing degradation: %s",
            exception.what()
        );
        return false;
    } catch (...) {
        if (error) *error = "Unknown exception while preparing pass degradation";
        tc::Log::error("[UnknownPass] Unknown exception while preparing degradation");
        return false;
    }

    if (!prepared.validate(error)) {
        tc::Log::error(
            "[UnknownPass] Prepared plan rejected: %s",
            error ? error->c_str() : "unknown validation error"
        );
        return false;
    }
    plan = std::move(prepared);
    return true;
}

UnknownPassStats degrade_passes_to_unknown(
    const std::vector<std::string>& type_names
) {
    UnknownPassStats stats;
    UnknownPassDegradationPlan plan;
    std::string error;
    if (!prepare_passes_to_unknown(type_names, plan, &error)) {
        stats.failed = 1;
        tc::Log::error(
            "[UnknownPass] Failed to prepare pass degradation: %s",
            error.c_str()
        );
        return stats;
    }
    stats.degraded = plan.size();
    if (!plan.commit(&error)) {
        stats.failed = plan.size() == 0 ? 1 : plan.size();
        stats.degraded = 0;
        tc::Log::error(
            "[UnknownPass] Failed to commit pass degradation: %s",
            error.c_str()
        );
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
