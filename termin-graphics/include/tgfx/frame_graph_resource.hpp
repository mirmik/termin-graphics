#pragma once

#include <string>

namespace termin {

// Base class for all framegraph resources.
// Resources are passed between passes via reads/writes maps.
class FrameGraphResource {
public:
    virtual ~FrameGraphResource() = default;

    // Resource type identifier.
    // Used by passes to determine how to handle the resource.
    virtual const char* resource_type() const = 0;
};

} // namespace termin
