#pragma once

#include <type_traits>
#include <vector>

#include <termin/render/render_export.hpp>

namespace termin {

// Marker base for adapter-owned services borrowed by one render execution.
// The registry exposes only type-safe lookup; generic execution never needs
// to know which concrete adapter capabilities are present.
class RENDER_API RenderExecutionCapability {
public:
    virtual ~RenderExecutionCapability() = default;
};

class RENDER_API RenderExecutionCapabilities {
private:
    std::vector<const RenderExecutionCapability*> values_;

public:
    void add(const RenderExecutionCapability& capability);

    template <typename T>
    const T* find() const {
        static_assert(std::is_base_of_v<RenderExecutionCapability, T>);
        for (const RenderExecutionCapability* capability : values_) {
            if (const auto* typed = dynamic_cast<const T*>(capability)) {
                return typed;
            }
        }
        return nullptr;
    }
};

} // namespace termin
