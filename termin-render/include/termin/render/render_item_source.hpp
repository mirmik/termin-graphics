#pragma once

#include <cstdint>

#include <termin/render/render_camera.hpp>
#include <termin/render/render_export.hpp>
#include <termin/render/render_item_snapshot.hpp>

namespace termin {

// Scene-neutral view and filtering inputs for one immutable item snapshot.
// Every pointer is borrowed only for the synchronous publish() call.
struct RenderItemSourceRequest {
    const RenderViewState* view = nullptr;
    uint64_t layer_mask = UINT64_MAX;
    uint64_t render_category_mask = UINT64_MAX;
    const char* debug_name = nullptr;
};

// Publishes a complete immutable snapshot through one guarded lifecycle.
// Implementations collect into the supplied storage but cannot publish a
// partial snapshot themselves.
class RENDER_CORE_API RenderItemSource {
public:
    virtual ~RenderItemSource();

    bool publish(
        RenderItemSnapshot& snapshot,
        const RenderItemSourceRequest& request);

protected:
    virtual const char* source_name() const noexcept = 0;
    virtual bool collect_items(
        const RenderItemSourceRequest& request,
        RenderItemCollection& output,
        RenderItemSnapshotCounters& counters) = 0;
};

} // namespace termin
