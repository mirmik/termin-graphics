#include <tgfx2/clip_space.hpp>

namespace tgfx {

termin::Mat44f adapt_projection_for_backend(
    BackendType backend,
    const termin::Mat44f& projection)
{
    if (backend != BackendType::D3D11) {
        return projection;
    }

    termin::Mat44f flip = termin::Mat44f::identity();
    flip(1, 1) = -1.0f;
    return flip * projection;
}

termin::Mat44 adapt_projection_for_backend(
    BackendType backend,
    const termin::Mat44& projection)
{
    if (backend != BackendType::D3D11) {
        return projection;
    }

    termin::Mat44 flip = termin::Mat44::identity();
    flip(1, 1) = -1.0;
    return flip * projection;
}

} // namespace tgfx
