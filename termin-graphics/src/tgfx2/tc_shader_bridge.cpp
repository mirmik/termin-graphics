// tc_shader_bridge.cpp - TcShader ↔ tgfx2 interop.
#include "tgfx2/tc_shader_bridge.hpp"

#include "tgfx2/i_render_device.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" {
#include "tgfx/resources/tc_shader.h"
#include <tcbase/tc_log.h>
}

namespace termin {

static std::string g_shader_artifact_root;

void tgfx2_set_shader_artifact_root(const char* root) {
    g_shader_artifact_root = root ? root : "";
}

const char* tgfx2_get_shader_artifact_root(void) {
    if (!g_shader_artifact_root.empty()) {
        return g_shader_artifact_root.c_str();
    }
    const char* env = std::getenv("TERMIN_SHADER_ARTIFACT_ROOT");
    return env ? env : "";
}

static const char* stage_extension(tgfx::ShaderStage stage) {
    switch (stage) {
        case tgfx::ShaderStage::Vertex: return "vert";
        case tgfx::ShaderStage::Fragment: return "frag";
        case tgfx::ShaderStage::Geometry: return "geom";
        case tgfx::ShaderStage::Compute: return "comp";
    }
    return "spv";
}

bool tgfx2_load_shader_artifact(
    const char* shader_uuid,
    tgfx::ShaderStage stage,
    std::vector<uint8_t>& out
) {
    const char* root = tgfx2_get_shader_artifact_root();
    if (!shader_uuid || shader_uuid[0] == '\0') {
        tc_log(TC_LOG_ERROR,
               "tc_shader_ensure_tgfx2: cannot load SPIR-V artifact, shader_uuid='%s'",
               shader_uuid ? shader_uuid : "<null>");
        return false;
    }
    if (!root || root[0] == '\0') {
        return false;
    }

    std::string path = std::string(root) + "/shaders/vulkan/"
        + shader_uuid + "." + stage_extension(stage) + ".spv";

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: missing shader artifact '%s'", path.c_str());
        return false;
    }

    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out.empty()) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: empty shader artifact '%s'", path.c_str());
        return false;
    }
    return true;
}

bool tc_shader_ensure_tgfx2(
    ::tc_shader* shader,
    tgfx::IRenderDevice* device,
    tgfx::ShaderHandle* out_vs,
    tgfx::ShaderHandle* out_fs)
{
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: shader is NULL");
        return false;
    }
    if (!device) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: device is NULL");
        return false;
    }
    if (!out_fs) {
        tc_log(TC_LOG_ERROR, "tc_shader_ensure_tgfx2: out_fs is NULL");
        return false;
    }
    return device->ensure_tc_shader(shader, out_vs, out_fs);
}

} // namespace termin
