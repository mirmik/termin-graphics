"""TextureHandle - re-export from C++."""
from termin._native.assets import TextureHandle

# Singleton for white texture handle
_white_texture_handle: TextureHandle | None = None

# Singleton for normal texture handle
_normal_texture_handle: TextureHandle | None = None


def get_white_texture_handle() -> TextureHandle:
    """Return a TextureHandle for the white 1x1 texture singleton."""
    global _white_texture_handle
    if _white_texture_handle is None:
        from termin.render.texture import get_white_texture
        white_tex = get_white_texture()
        _white_texture_handle = white_tex._handle
    return _white_texture_handle


def get_normal_texture_handle() -> TextureHandle:
    """Return a TextureHandle for the flat normal 1x1 texture singleton."""
    global _normal_texture_handle
    if _normal_texture_handle is None:
        from termin.render.texture import get_normal_texture
        normal_tex = get_normal_texture()
        _normal_texture_handle = normal_tex._handle
    return _normal_texture_handle


__all__ = ["TextureHandle", "get_white_texture_handle", "get_normal_texture_handle"]
