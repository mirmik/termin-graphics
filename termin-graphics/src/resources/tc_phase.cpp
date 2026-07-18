#include "tgfx/resources/tc_phase.h"

#include <array>
#include <bit>
#include <cctype>
#include <mutex>
#include <string>
#include <string_view>

#include <tcbase/tc_log.h>

namespace {

struct PhaseRegistry {
    std::mutex mutex;
    std::array<std::string, TC_PHASE_INDEX_COUNT> names;

    PhaseRegistry()
    {
        names[TC_PHASE_INDEX_OPAQUE] = "opaque";
        names[TC_PHASE_INDEX_TRANSPARENT] = "transparent";
        names[TC_PHASE_INDEX_NORMAL] = "normal";
        names[TC_PHASE_INDEX_DEPTH] = "depth";
        names[TC_PHASE_INDEX_ID] = "id";
        names[TC_PHASE_INDEX_SHADOW] = "shadow";
        names[TC_PHASE_INDEX_UI] = "ui";
        names[TC_PHASE_INDEX_EDITOR] = "editor";
        names[TC_PHASE_INDEX_EDITOR_DEBUG] = "editor_debug";
        names[TC_PHASE_INDEX_EDITOR_DEBUG_TRANSPARENT] =
            "editor_debug_transparent";
    }
};

PhaseRegistry& registry()
{
    static PhaseRegistry value;
    return value;
}

bool valid_phase_name(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    for (char ch : name) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (!(std::isalnum(value) || ch == '_' || ch == '-' || ch == '.')) {
            return false;
        }
    }
    return true;
}

tc_phase_mask find_locked(const PhaseRegistry& value, std::string_view name)
{
    for (uint32_t index = 0; index < TC_PHASE_INDEX_COUNT; ++index) {
        if (!value.names[index].empty() && value.names[index] == name) {
            return UINT64_C(1) << index;
        }
    }
    return TC_PHASE_NONE;
}

} // namespace

extern "C" tc_phase_mask tc_phase_find(const char* name)
{
    if (!name || name[0] == '\0') {
        return TC_PHASE_NONE;
    }
    PhaseRegistry& value = registry();
    std::lock_guard<std::mutex> lock(value.mutex);
    return find_locked(value, name);
}

extern "C" bool tc_phase_set_project_name(uint32_t project_index, const char* name)
{
    if (project_index >= TC_PHASE_PROJECT_CAPACITY) {
        tc_log(TC_LOG_ERROR,
               "[PhaseRegistry] project phase index %u is outside [0, %u)",
               project_index,
               TC_PHASE_PROJECT_CAPACITY);
        return false;
    }

    PhaseRegistry& value = registry();
    std::lock_guard<std::mutex> lock(value.mutex);
    const uint32_t index = TC_PHASE_INDEX_USER_FIRST + project_index;
    if (!name || name[0] == '\0') {
        value.names[index].clear();
        return true;
    }
    if (!valid_phase_name(name)) {
        tc_log(TC_LOG_ERROR, "[PhaseRegistry] invalid project phase name '%s'", name);
        return false;
    }
    const tc_phase_mask existing = find_locked(value, name);
    if (existing != TC_PHASE_NONE && existing != (UINT64_C(1) << index)) {
        tc_log(TC_LOG_ERROR,
               "[PhaseRegistry] phase name '%s' is already assigned to bit %u",
               name,
               static_cast<unsigned>(std::countr_zero(existing)));
        return false;
    }
    value.names[index] = name;
    return true;
}

extern "C" const char* tc_phase_name(tc_phase_mask phase)
{
    if (!tc_phase_is_single(phase)) {
        return nullptr;
    }
    const uint32_t index = static_cast<uint32_t>(std::countr_zero(phase));
    PhaseRegistry& value = registry();
    std::lock_guard<std::mutex> lock(value.mutex);
    return value.names[index].empty() ? nullptr : value.names[index].c_str();
}

extern "C" bool tc_phase_is_single(tc_phase_mask phase)
{
    return phase != TC_PHASE_NONE && (phase & (phase - 1u)) == 0u;
}

extern "C" bool tc_phase_mask_contains(tc_phase_mask mask, tc_phase_mask phase)
{
    return tc_phase_is_single(phase) && (mask & phase) != 0u;
}

extern "C" void tc_phase_clear_project_registry(void)
{
    PhaseRegistry& value = registry();
    std::lock_guard<std::mutex> lock(value.mutex);
    for (uint32_t index = TC_PHASE_INDEX_USER_FIRST;
         index < TC_PHASE_INDEX_COUNT;
         ++index) {
        value.names[index].clear();
    }
}
