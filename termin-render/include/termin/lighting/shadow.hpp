#pragma once

#include <vector>

#include <termin/geom/mat44.hpp>
#include <termin/render/render_export.hpp>

#include "termin/lighting/shadow_settings.hpp"
#include "tgfx/frame_graph_resource.hpp"
#include "tgfx2/handles.hpp"

namespace termin {

struct RENDER_API ShadowMapArrayEntry {
public:
    tgfx::TextureHandle depth_tex2;
    int width = 0;
    int height = 0;
    Mat44f light_space_matrix;
    int light_index = 0;
    int cascade_index = 0;
    float cascade_split_near = 0.0f;
    float cascade_split_far = 0.0f;

public:
    ShadowMapArrayEntry() = default;

    ShadowMapArrayEntry(
        tgfx::TextureHandle depth,
        int w, int h,
        const Mat44f& matrix,
        int light_idx,
        int cascade_idx = 0,
        float split_near = 0.0f,
        float split_far = 0.0f
    ) : depth_tex2(depth),
        width(w),
        height(h),
        light_space_matrix(matrix),
        light_index(light_idx),
        cascade_index(cascade_idx),
        cascade_split_near(split_near),
        cascade_split_far(split_far) {}

};

class RENDER_API ShadowMapArrayResource : public FrameGraphResource {
public:
    std::vector<ShadowMapArrayEntry> entries;
    int resolution = 1024;

public:
    ShadowMapArrayResource() = default;
    explicit ShadowMapArrayResource(int res) : resolution(res) {}

    const char* resource_type() const override { return "shadow_map_array"; }

    size_t size() const { return entries.size(); }
    bool empty() const { return entries.empty(); }

    void clear() {
        entries.clear();
    }

    void add_entry(
        tgfx::TextureHandle depth_tex2,
        int width,
        int height,
        const Mat44f& light_space_matrix,
        int light_index,
        int cascade_index = 0,
        float cascade_split_near = 0.0f,
        float cascade_split_far = 0.0f
    ) {
        entries.push_back(ShadowMapArrayEntry(
            depth_tex2, width, height,
            light_space_matrix, light_index, cascade_index,
            cascade_split_near, cascade_split_far
        ));
    }

    const ShadowMapArrayEntry& operator[](size_t index) const {
        return entries[index];
    }

    ShadowMapArrayEntry& operator[](size_t index) {
        return entries[index];
    }

    ShadowMapArrayEntry* get_by_light_index(int light_index) {
        for (auto& entry : entries) {
            if (entry.light_index == light_index) {
                return &entry;
            }
        }
        return nullptr;
    }

    size_t __len__() const { return size(); }

    auto begin() { return entries.begin(); }
    auto end() { return entries.end(); }
    auto begin() const { return entries.begin(); }
    auto end() const { return entries.end(); }
};

} // namespace termin
