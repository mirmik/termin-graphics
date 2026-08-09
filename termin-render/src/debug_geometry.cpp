#include "termin/render/debug_geometry.hpp"

#include <algorithm>
#include <utility>

namespace termin {

    DebugGeometryTypeRegistration::DebugGeometryTypeRegistration(const char* stable_id,
                                                                 const char* display_name,
                                                                 const char* category,
                                                                 bool default_enabled)
        : type_id_(tc_debug_geometry_type_register(stable_id, display_name, category, default_enabled)) {}

    DebugGeometryTypeRegistration::~DebugGeometryTypeRegistration() {
        if (tc_debug_geometry_type_registered(type_id_)) {
            tc_debug_geometry_type_unregister(type_id_);
        }
    }

    DebugGeometryTypeRegistration::DebugGeometryTypeRegistration(DebugGeometryTypeRegistration&& other) noexcept
        : type_id_(std::exchange(other.type_id_, TC_DEBUG_GEOMETRY_TYPE_INVALID)) {}

    DebugGeometryTypeRegistration&
    DebugGeometryTypeRegistration::operator=(DebugGeometryTypeRegistration&& other) noexcept {
        if (this == &other)
            return *this;
        if (tc_debug_geometry_type_registered(type_id_)) {
            tc_debug_geometry_type_unregister(type_id_);
        }
        type_id_ = std::exchange(other.type_id_, TC_DEBUG_GEOMETRY_TYPE_INVALID);
        return *this;
    }

    bool DebugGeometryDrawer::line(const Vec3& start, const Vec3& end, const SrgbColor& color, bool depth_test) const {
        const float start_data[3] = {
            static_cast<float>(start.x),
            static_cast<float>(start.y),
            static_cast<float>(start.z),
        };
        const float end_data[3] = {
            static_cast<float>(end.x),
            static_cast<float>(end.y),
            static_cast<float>(end.z),
        };
        const float color_data[4] = {color.r, color.g, color.b, color.a};
        return tc_debug_geometry_drawer_line(&drawer_, start_data, end_data, color_data, depth_test);
    }

    bool DebugGeometryDrawer::wire_sphere(
        const Vec3& center, double radius, const SrgbColor& color, int segments, bool depth_test) const {
        const float center_data[3] = {
            static_cast<float>(center.x),
            static_cast<float>(center.y),
            static_cast<float>(center.z),
        };
        const float color_data[4] = {color.r, color.g, color.b, color.a};
        const uint16_t segment_count = static_cast<uint16_t>(std::clamp(segments, 3, 65535));
        return tc_debug_geometry_drawer_wire_sphere(
            &drawer_, center_data, static_cast<float>(radius), color_data, segment_count, depth_test);
    }

    bool DebugGeometryDrawer::wire_box(const Vec3& center,
                                       const Vec3& half_axis_x,
                                       const Vec3& half_axis_y,
                                       const Vec3& half_axis_z,
                                       const SrgbColor& color,
                                       bool depth_test) const {
        const float center_data[3] = {
            static_cast<float>(center.x),
            static_cast<float>(center.y),
            static_cast<float>(center.z),
        };
        const float axis_x_data[3] = {
            static_cast<float>(half_axis_x.x),
            static_cast<float>(half_axis_x.y),
            static_cast<float>(half_axis_x.z),
        };
        const float axis_y_data[3] = {
            static_cast<float>(half_axis_y.x),
            static_cast<float>(half_axis_y.y),
            static_cast<float>(half_axis_y.z),
        };
        const float axis_z_data[3] = {
            static_cast<float>(half_axis_z.x),
            static_cast<float>(half_axis_z.y),
            static_cast<float>(half_axis_z.z),
        };
        const float color_data[4] = {color.r, color.g, color.b, color.a};
        return tc_debug_geometry_drawer_wire_box(
            &drawer_, center_data, axis_x_data, axis_y_data, axis_z_data, color_data, depth_test);
    }

    bool DebugGeometryDrawer::wire_capsule(
        const Vec3& start, const Vec3& end, double radius, const SrgbColor& color, int segments, bool depth_test) const {
        const float start_data[3] = {
            static_cast<float>(start.x),
            static_cast<float>(start.y),
            static_cast<float>(start.z),
        };
        const float end_data[3] = {
            static_cast<float>(end.x),
            static_cast<float>(end.y),
            static_cast<float>(end.z),
        };
        const float color_data[4] = {color.r, color.g, color.b, color.a};
        const uint16_t segment_count = static_cast<uint16_t>(std::clamp(segments, 4, 65535));
        return tc_debug_geometry_drawer_wire_capsule(
            &drawer_, start_data, end_data, static_cast<float>(radius), color_data, segment_count, depth_test);
    }

} // namespace termin
