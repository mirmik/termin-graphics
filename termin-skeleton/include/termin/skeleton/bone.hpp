#pragma once

#include <string>
#include <array>
#include <cstring>

namespace termin {

/**
 * Single bone in a skeleton hierarchy.
 *
 * Stores bone metadata and bind pose information.
 * All matrices and vectors use double precision for compatibility with numpy.
 */
struct Bone {
    std::string name;
    int index = 0;
    int parent_index = -1;  // -1 for root bones

    // 4x4 inverse bind matrix (column-major, matches OpenGL/numpy convention)
    std::array<double, 16> inverse_bind_matrix;

    // Bind pose local transform
    std::array<double, 3> bind_translation = {0.0, 0.0, 0.0};
    std::array<double, 4> bind_rotation = {0.0, 0.0, 0.0, 1.0};  // [x, y, z, w]
    std::array<double, 3> bind_scale = {1.0, 1.0, 1.0};

    Bone() {
        // Initialize inverse_bind_matrix to identity
        inverse_bind_matrix.fill(0.0);
        inverse_bind_matrix[0] = 1.0;
        inverse_bind_matrix[5] = 1.0;
        inverse_bind_matrix[10] = 1.0;
        inverse_bind_matrix[15] = 1.0;
    }

    Bone(const std::string& name_, int index_, int parent_index_)
        : name(name_), index(index_), parent_index(parent_index_) {
        inverse_bind_matrix.fill(0.0);
        inverse_bind_matrix[0] = 1.0;
        inverse_bind_matrix[5] = 1.0;
        inverse_bind_matrix[10] = 1.0;
        inverse_bind_matrix[15] = 1.0;
    }

    bool is_root() const {
        return parent_index < 0;
    }
};

} // namespace termin
