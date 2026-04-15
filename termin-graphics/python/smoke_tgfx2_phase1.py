"""Phase 1 smoke test for FontTextureAtlas.ensure_texture_tgfx2.

Creates a minimal GL context, spins up a Tgfx2Context, loads the
default font, uploads it through the tgfx2 path, then adds a glyph
and verifies the re-upload path.

No visual check — validation is programmatic. Visual proof of the
atlas actually sampling correctly comes in Phase 2 (Text3D port) and
Phase 3 (Text2D).

Usage:
  python termin-graphics/python/smoke_tgfx2_phase1.py
"""

import ctypes
import sys

import sdl2
from sdl2 import video

from tgfx import OpenGLGraphicsBackend
from tgfx.font import get_default_font
from tgfx._tgfx_native import Tgfx2Context


def create_window(title, width, height):
    if sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO) != 0:
        raise RuntimeError(f"SDL_Init failed: {sdl2.SDL_GetError()}")
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MAJOR_VERSION, 3)
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MINOR_VERSION, 3)
    video.SDL_GL_SetAttribute(
        video.SDL_GL_CONTEXT_PROFILE_MASK,
        video.SDL_GL_CONTEXT_PROFILE_CORE,
    )
    video.SDL_GL_SetAttribute(video.SDL_GL_DOUBLEBUFFER, 1)
    flags = video.SDL_WINDOW_OPENGL | video.SDL_WINDOW_HIDDEN
    window = video.SDL_CreateWindow(
        title.encode(),
        video.SDL_WINDOWPOS_CENTERED,
        video.SDL_WINDOWPOS_CENTERED,
        width,
        height,
        flags,
    )
    gl_ctx = video.SDL_GL_CreateContext(window)
    video.SDL_GL_MakeCurrent(window, gl_ctx)
    return window, gl_ctx


def main() -> int:
    window, gl_ctx = create_window("tgfx2 Phase 1 smoke (hidden)", 320, 240)

    graphics = OpenGLGraphicsBackend.get_instance()
    graphics.ensure_ready()

    print("[1] constructing Tgfx2Context...")
    holder = Tgfx2Context()

    print("[2] loading default font...")
    font = get_default_font(size=14)
    if font is None:
        print("    ERROR: no system font available")
        return 1
    print(f"    glyph count after preload: {len(font.glyphs)}")

    print("[3] first ensure_texture_tgfx2...")
    handle = font.ensure_texture_tgfx2(holder)
    print(f"    handle.id = {handle.id}")
    if handle.id == 0:
        print("    ERROR: null handle")
        return 1

    print("[4] caching check — same handle on repeat call...")
    handle2 = font.ensure_texture_tgfx2(holder)
    if handle2.id != handle.id:
        print(f"    ERROR: handle changed {handle.id} -> {handle2.id}")
        return 1
    print("    same handle returned")

    print("[5] add new glyph, verify re-upload doesn't crash...")
    # A character guaranteed not in the preload set.
    font.ensure_glyphs("\u6C34", graphics=None, tgfx2_ctx=holder)  # 水
    if "\u6C34" not in font.glyphs:
        print("    NOTE: glyph not supported by font (not an error)")
    # After ensure_glyphs the tgfx2 upload already ran via tgfx2_ctx.
    # Call ensure_texture_tgfx2 once more to exercise the "dirty
    # already cleared" branch.
    handle3 = font.ensure_texture_tgfx2(holder)
    if handle3.id != handle.id:
        print(f"    ERROR: handle changed {handle.id} -> {handle3.id}")
        return 1
    print("    re-upload clean, same handle")

    print("[6] tgfx1 path still works (coexistence check)...")
    tgfx1_handle = font.ensure_texture(graphics)
    print(f"    tgfx1 handle.id = {tgfx1_handle.get_id()}")

    print("[7] destroying handle explicitly...")
    holder.destroy_texture(handle)

    print("[cleanup] tearing down GL context")
    del holder
    video.SDL_GL_DeleteContext(gl_ctx)
    video.SDL_DestroyWindow(window)
    sdl2.SDL_Quit()
    print("Phase 1 smoke: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
