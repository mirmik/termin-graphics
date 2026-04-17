// tgfx2_interop.h - Bridge between legacy tgfx gpu_ops and tgfx2 IRenderDevice
// Allows the resource system (registries, tc_gpu_context) to route GPU operations
// through tgfx2 while maintaining backward compatibility with GL IDs.
#pragma once

#include "tgfx/tgfx_api.h"

#ifdef __cplusplus

namespace tgfx { class IRenderDevice; }

extern "C" {
#endif

// Set the tgfx2 render device used by the tgfx2-backed gpu_ops implementation.
// Must be called before tgfx2_gpu_ops_register().
TGFX_API void tgfx2_interop_set_device(void* device);

// Get the tgfx2 render device (returns NULL if not set).
TGFX_API void* tgfx2_interop_get_device(void);

// Register tgfx2-backed gpu_ops vtable.
// Requires tgfx2_interop_set_device() to have been called first.
// The vtable routes resource creation through tgfx::IRenderDevice
// and extracts GL IDs for backward compatibility with existing code.
TGFX_API void tgfx2_gpu_ops_register(void);

#ifdef __cplusplus
}

// C++ typed accessors
namespace tgfx {

inline void set_tgfx2_device(tgfx::IRenderDevice* device) {
    tgfx2_interop_set_device(static_cast<void*>(device));
}

inline tgfx::IRenderDevice* get_tgfx2_device() {
    return static_cast<tgfx::IRenderDevice*>(tgfx2_interop_get_device());
}

} // namespace tgfx
#endif
