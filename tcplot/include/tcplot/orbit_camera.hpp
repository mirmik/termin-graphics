#pragma once

#include "tcplot/tcplot_api.h"
#include <termin/camera/orbit_camera.hpp>

namespace tcplot {

class TCPLOT_API OrbitCamera : public termin::OrbitCamera {
public:
    float& near;
    float& far;

    OrbitCamera()
        : termin::OrbitCamera(), near(near_clip), far(far_clip) {}

    OrbitCamera(const OrbitCamera& other)
        : termin::OrbitCamera(other), near(near_clip), far(far_clip) {}

    OrbitCamera& operator=(const OrbitCamera& other) {
        termin::OrbitCamera::operator=(other);
        return *this;
    }
};

}  // namespace tcplot
