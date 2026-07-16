#pragma once

#include <cstdint>

namespace tgfx {

// Handles are local to the IRenderDevice that created them. Their compact IDs
// carry no device identity and must never be resolved by another device. The
// application graphics domain therefore installs exactly one shared device;
// standalone test/tool devices remain isolated from application interop.

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

} // namespace tgfx
