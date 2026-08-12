#pragma once

#include <cstdint>

#include "tgfx2/enums.hpp"

namespace tgfx {

    // Shader artifact identity is deliberately independent of BackendType.
    // A GL-family render device can therefore keep its backend identity while
    // selecting the exact offline shader profile it consumes.
    enum class ShaderArtifactTarget : uint8_t {
        None,
        Vulkan,
        OpenGL450,
        OpenGL330,
        WebGL2,
        D3D11,
        WebGPU,
    };

    inline constexpr ShaderArtifactTarget shader_artifact_target_for_backend(BackendType backend) {
        switch (backend) {
        case BackendType::OpenGL:
            return ShaderArtifactTarget::OpenGL450;
        case BackendType::Vulkan:
            return ShaderArtifactTarget::Vulkan;
        case BackendType::D3D11:
            return ShaderArtifactTarget::D3D11;
        case BackendType::WebGPU:
            return ShaderArtifactTarget::WebGPU;
        case BackendType::Metal:
        case BackendType::Null:
            return ShaderArtifactTarget::None;
        }
        return ShaderArtifactTarget::None;
    }

    inline constexpr const char* shader_artifact_target_name(ShaderArtifactTarget target) {
        switch (target) {
        case ShaderArtifactTarget::Vulkan:
            return "vulkan";
        case ShaderArtifactTarget::OpenGL450:
            return "opengl450";
        case ShaderArtifactTarget::OpenGL330:
            return "opengl330";
        case ShaderArtifactTarget::WebGL2:
            return "webgl2";
        case ShaderArtifactTarget::D3D11:
            return "d3d11";
        case ShaderArtifactTarget::WebGPU:
            return "webgpu";
        case ShaderArtifactTarget::None:
            return "none";
        }
        return "none";
    }

    // OpenGL450 retains the historical directory name so existing SDKs and
    // application packages remain valid. New profiles get distinct roots.
    inline constexpr const char* shader_artifact_target_directory(ShaderArtifactTarget target) {
        switch (target) {
        case ShaderArtifactTarget::OpenGL450:
            return "opengl";
        case ShaderArtifactTarget::Vulkan:
        case ShaderArtifactTarget::OpenGL330:
        case ShaderArtifactTarget::WebGL2:
        case ShaderArtifactTarget::D3D11:
        case ShaderArtifactTarget::WebGPU:
            return shader_artifact_target_name(target);
        case ShaderArtifactTarget::None:
            return "";
        }
        return "";
    }

} // namespace tgfx
