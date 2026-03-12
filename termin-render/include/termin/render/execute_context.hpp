#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <termin/render/frame_pass.hpp>
#include <termin/render/light.hpp>
#include <termin/tc_scene.hpp>
#include <core/tc_entity_pool.h>

namespace termin {

class CameraComponent;

struct ExecuteContext {
public:
    GraphicsBackend* graphics = nullptr;
    FBOMap reads_fbos;
    FBOMap writes_fbos;
    Rect4i rect;
    TcSceneRef scene;
    CameraComponent* camera = nullptr;
    std::string viewport_name;
    tc_entity_handle internal_entities = TC_ENTITY_HANDLE_INVALID;
    std::vector<Light> lights;
    uint64_t layer_mask = 0xFFFFFFFFFFFFFFFFULL;
};

} // namespace termin
