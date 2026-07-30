#include <termin/materials/surface_contract_registry.hpp>

#include <cassert>
#include <cstdlib>

int main() {
    using termin::SurfaceContractDescriptor;
    using termin::SurfaceContractKey;
    using termin::SurfaceContractRegistry;

    tc_surface_contract_registry_clear();

    SurfaceContractDescriptor descriptor{
        {"game.surface.cpp-probe", 7},
        "C++ Probe",
        "GameCppSurfaceV7",
        "struct GameCppSurfaceV7 { float custom_field; };",
        "cpp-probe-v7-source-a",
    };
    assert(SurfaceContractRegistry::register_contract(
        descriptor,
        "game-cpp-probe"
    ));

    descriptor.debug_name = "mutated caller descriptor";
    const auto stored = SurfaceContractRegistry::find(
        SurfaceContractKey{"game.surface.cpp-probe", 7}
    );
    assert(stored);
    assert(stored->debug_name == "C++ Probe");
    assert(stored->interface_source.find("custom_field") != std::string::npos);
    assert(SurfaceContractRegistry::owner_of(stored->key) == "game-cpp-probe");

    assert(SurfaceContractRegistry::unregister_contract(
        stored->key,
        "game-cpp-probe"
    ));
    assert(!SurfaceContractRegistry::find(stored->key));
    return EXIT_SUCCESS;
}
