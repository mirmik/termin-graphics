#pragma once

#include "termin/render/render_export.hpp"

#include "core/tc_debug_geometry.h"

#include <termin/geom/vec3.hpp>
#include <tgfx/types.hpp>

namespace termin {

    class RENDER_API DebugGeometryTypeRegistration {
    private:
        tc_debug_geometry_type_id type_id_ = TC_DEBUG_GEOMETRY_TYPE_INVALID;

    public:
        DebugGeometryTypeRegistration(const char* stable_id,
                                      const char* display_name,
                                      const char* category,
                                      bool default_enabled = true);
        ~DebugGeometryTypeRegistration();

        DebugGeometryTypeRegistration(const DebugGeometryTypeRegistration&) = delete;
        DebugGeometryTypeRegistration& operator=(const DebugGeometryTypeRegistration&) = delete;
        DebugGeometryTypeRegistration(DebugGeometryTypeRegistration&& other) noexcept;
        DebugGeometryTypeRegistration& operator=(DebugGeometryTypeRegistration&& other) noexcept;

        tc_debug_geometry_type_id type_id() const {
            return type_id_;
        }
        bool valid() const {
            return tc_debug_geometry_type_registered(type_id_);
        }
    };

    class RENDER_API DebugGeometryDrawer {
    private:
        tc_debug_geometry_drawer drawer_ = {
            TC_SCENE_HANDLE_INVALID,
            TC_DEBUG_GEOMETRY_TYPE_INVALID,
        };

    public:
        DebugGeometryDrawer() = default;
        explicit DebugGeometryDrawer(tc_debug_geometry_drawer drawer)
            : drawer_(drawer) {}

        bool valid() const {
            return tc_debug_geometry_drawer_valid(&drawer_);
        }
        explicit operator bool() const {
            return valid();
        }

        bool line(const Vec3& start, const Vec3& end, const SrgbColor& color, bool depth_test = false) const;
        bool wire_sphere(
            const Vec3& center, double radius, const SrgbColor& color, int segments = 16, bool depth_test = false) const;
        bool wire_box(const Vec3& center,
                      const Vec3& half_axis_x,
                      const Vec3& half_axis_y,
                      const Vec3& half_axis_z,
                      const SrgbColor& color,
                      bool depth_test = false) const;
        bool wire_capsule(const Vec3& start,
                          const Vec3& end,
                          double radius,
                          const SrgbColor& color,
                          int segments = 16,
                          bool depth_test = false) const;
    };

} // namespace termin
