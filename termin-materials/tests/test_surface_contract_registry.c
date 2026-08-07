#include <termin/materials/surface_contract_registry.h>

#include <inspect/tc_runtime_type_registry.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static tc_surface_contract_desc descriptor(const char* id,
                                           uint32_t version,
                                           const char* debug_name,
                                           const char* type_name,
                                           const char* source,
                                           const char* source_identity) {
    tc_surface_contract_desc result = {
        TC_SURFACE_CONTRACT_DESCRIPTOR_ABI_VERSION,
        {id, version},
        debug_name,
        type_name,
        source,
        source_identity,
    };
    return result;
}

int main(void) {
    tc_surface_contract_registry_clear();
    assert(tc_surface_contract_registry_count() == 0);

    assert(tc_surface_contract_registry_register_builtins());
    const tc_surface_contract_key standard_key = {
        TC_STANDARD_PBR_SURFACE_CONTRACT_ID,
        TC_STANDARD_PBR_SURFACE_CONTRACT_VERSION,
    };
    const tc_surface_contract_desc* standard = tc_surface_contract_registry_find(standard_key);
    assert(standard);
    assert(strcmp(standard->surface_type_name, "TerminStandardSurfaceV1") == 0);
    assert(strstr(standard->interface_source, "float3 base_color") != NULL);
    assert(standard->source_identity[0] != '\0');
    assert(strcmp(tc_surface_contract_registry_owner(standard_key), "termin-materials") == 0);

    char id[] = "game.surface.weathered";
    char debug_name[] = "Weathered v1";
    char type_name[] = "GameWeatheredSurfaceV1";
    char source[] = "struct GameWeatheredSurfaceV1 { float wetness; };";
    char source_identity[] = "weathered-v1-source-a";
    tc_surface_contract_desc version_one = descriptor(id, 1, debug_name, type_name, source, source_identity);
    assert(tc_surface_contract_registry_register("game-weather", &version_one));

    id[0] = 'X';
    debug_name[0] = 'X';
    type_name[0] = 'X';
    source[0] = 'X';
    source_identity[0] = 'X';

    const tc_surface_contract_key version_one_key = {
        "game.surface.weathered",
        1,
    };
    const tc_surface_contract_desc* copied = tc_surface_contract_registry_find(version_one_key);
    assert(copied);
    assert(strcmp(copied->debug_name, "Weathered v1") == 0);
    assert(strcmp(copied->surface_type_name, "GameWeatheredSurfaceV1") == 0);
    assert(strcmp(copied->interface_source, "struct GameWeatheredSurfaceV1 { float wetness; };") == 0);
    assert(strcmp(copied->source_identity, "weathered-v1-source-a") == 0);

    const tc_surface_contract_desc identical = descriptor("game.surface.weathered",
                                                          1,
                                                          "Weathered v1",
                                                          "GameWeatheredSurfaceV1",
                                                          "struct GameWeatheredSurfaceV1 { float wetness; };",
                                                          "weathered-v1-source-a");
    assert(tc_surface_contract_registry_register("game-weather", &identical));
    assert(!tc_surface_contract_registry_register("another-owner", &identical));

    const tc_surface_contract_desc conflict =
        descriptor("game.surface.weathered",
                   1,
                   "Weathered v1",
                   "GameWeatheredSurfaceV1",
                   "struct GameWeatheredSurfaceV1 { float wetness; float snow; };",
                   "weathered-v1-source-b");
    assert(!tc_surface_contract_registry_register("game-weather", &conflict));

    const tc_surface_contract_desc version_two =
        descriptor("game.surface.weathered",
                   2,
                   "Weathered v2",
                   "GameWeatheredSurfaceV2",
                   "struct GameWeatheredSurfaceV2 { float wetness; float snow; };",
                   "weathered-v2-source-a");
    assert(tc_surface_contract_registry_register("game-weather", &version_two));
    assert(tc_surface_contract_registry_count() == 3);
    assert(tc_surface_contract_registry_find((tc_surface_contract_key){"game.surface.weathered", 2}));

    assert(!tc_surface_contract_registry_unregister("wrong-owner", version_one_key));
    assert(tc_surface_contract_registry_find(version_one_key));
    assert(tc_surface_contract_registry_unregister("game-weather", version_one_key));
    assert(!tc_surface_contract_registry_find(version_one_key));
    assert(!tc_surface_contract_registry_unregister("game-weather", version_one_key));

    assert(tc_surface_contract_registry_unregister_owner("game-weather") == 1);
    assert(!tc_surface_contract_registry_find((tc_surface_contract_key){"game.surface.weathered", 2}));
    assert(tc_surface_contract_registry_find(standard_key));

    const tc_surface_contract_desc module_owned = descriptor("game.surface.module-owned",
                                                             1,
                                                             "Module-owned Surface",
                                                             "GameModuleSurfaceV1",
                                                             "struct GameModuleSurfaceV1 { float project_field; };",
                                                             "module-owned-v1-source-a");
    assert(tc_surface_contract_registry_register("synthetic-project-module", &module_owned));
    assert(tc_runtime_type_registry_unregister_owner("synthetic-project-module") == 1);
    assert(!tc_surface_contract_registry_find(module_owned.key));

    tc_surface_contract_registry_clear();
    assert(tc_surface_contract_registry_count() == 0);
    return EXIT_SUCCESS;
}
