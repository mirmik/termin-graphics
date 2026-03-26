#pragma once

#include <cstdint>

namespace tgfx2 {

#define TGFX2_DEFINE_HANDLE(Name) \
    struct Name { \
        uint32_t id = 0; \
        explicit operator bool() const { return id != 0; } \
        bool operator==(const Name& o) const { return id == o.id; } \
        bool operator!=(const Name& o) const { return id != o.id; } \
    }

TGFX2_DEFINE_HANDLE(BufferHandle);
TGFX2_DEFINE_HANDLE(TextureHandle);
TGFX2_DEFINE_HANDLE(SamplerHandle);
TGFX2_DEFINE_HANDLE(ShaderHandle);
TGFX2_DEFINE_HANDLE(PipelineHandle);
TGFX2_DEFINE_HANDLE(ResourceSetHandle);
TGFX2_DEFINE_HANDLE(RenderTargetHandle);

#undef TGFX2_DEFINE_HANDLE

} // namespace tgfx2
