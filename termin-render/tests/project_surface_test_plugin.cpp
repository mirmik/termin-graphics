#include "project_surface_test_plugin.hpp"

#include <inspect/tc_runtime_type_registry.h>
#include <termin/materials/surface_contract_registry.h>

namespace {

    constexpr const char* kOwner = "termin-test-project-weathered";

    constexpr const char* kInterfaceSource = R"slang(
struct TestWeatheredSurfaceV1 {
    float3 base_color;
    float3 normal_world;
    float weathering;
};
)slang";

} // namespace

extern "C" {

bool termin_project_surface_test_plugin_register() {
    const tc_surface_contract_desc descriptor = {
        TC_SURFACE_CONTRACT_DESCRIPTOR_ABI_VERSION,
        {"test.surface.weathered", 1u},
        "Project Weathered Surface v1",
        "TestWeatheredSurfaceV1",
        kInterfaceSource,
        "test.surface.weathered@1:interface:v1",
    };
    return tc_surface_contract_registry_register(kOwner, &descriptor);
}

size_t termin_project_surface_test_plugin_unregister_owner() {
    return tc_runtime_type_registry_unregister_owner(kOwner);
}

const char* termin_project_surface_test_plugin_owner() {
    return kOwner;
}
}
