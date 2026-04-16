"""Smoke test for the C++ tgfx2::FontAtlas binding.

Creates an offscreen GL context, spins up a Tgfx2Context, loads the
default system font via the C++ FontAtlas, uploads it to the GPU,
exercises the glyph-addition + re-upload path, and checks metrics.

No visual check — validation is programmatic. Visual coverage comes
from running the tcgui demos and tcplot examples against this build.

Usage:
    python termin-graphics/python/smoke_font_atlas_cpp.py
"""
import sys

import sdl2
from sdl2 import video

from tgfx import OpenGLGraphicsBackend
from tgfx.font import get_default_font, FontTextureAtlas, find_system_font
from tgfx._tgfx_native import Tgfx2Context


def create_window(title: str, width: int, height: int):
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
        width, height, flags,
    )
    gl_ctx = video.SDL_GL_CreateContext(window)
    video.SDL_GL_MakeCurrent(window, gl_ctx)
    return window, gl_ctx


def main() -> int:
    window, gl_ctx = create_window("FontAtlas smoke (hidden)", 320, 240)

    graphics = OpenGLGraphicsBackend.get_instance()
    graphics.ensure_ready()

    print("[1] constructing Tgfx2Context...")
    holder = Tgfx2Context()
    ctx = holder.context

    print("[2] loading default system font...")
    path = find_system_font()
    print(f"    font path: {path}")
    font = get_default_font(size=14)
    if font is None:
        print("    ERROR: no system font available")
        return 1

    print(f"    rasterize_size = {font.size}")
    print(f"    ascent/descent = {font.ascent}/{font.descent}")
    print(f"    line_height    = {font.line_height}")
    print(f"    atlas size     = {font.atlas_width}x{font.atlas_height}")

    print("[3] measure_text('Hello', 14)...")
    w, h = font.measure_text("Hello", 14.0)
    print(f"    w={w:.2f}  h={h:.2f}")
    if w <= 0.0 or h <= 0.0:
        print("    ERROR: non-positive measurement")
        return 1

    print("[4] first ensure_texture(ctx)...")
    handle = font.ensure_texture(ctx)
    print(f"    handle.id = {handle.id}")
    if handle.id == 0:
        print("    ERROR: null handle")
        return 1

    print("[5] caching check — same handle on repeat call...")
    handle2 = font.ensure_texture(ctx)
    if handle2.id != handle.id:
        print(f"    ERROR: handle changed {handle.id} -> {handle2.id}")
        return 1
    print("    same handle returned")

    print("[6] add new glyph, verify re-upload path...")
    # A character very likely not in the preload set.
    font.ensure_glyphs("\u6C34", ctx=ctx)  # 水
    g = font.get_glyph(ord("\u6C34"))
    if g is None:
        print("    NOTE: glyph not supported by font (skipping dirty check)")
    else:
        print(f"    glyph 水 size = {g[4]:.1f}x{g[5]:.1f}")

    handle3 = font.ensure_texture(ctx)
    if handle3.id != handle.id:
        print(f"    ERROR: handle changed {handle.id} -> {handle3.id}")
        return 1
    print("    re-upload clean, same handle")

    print("[7] get_glyph for 'A'...")
    ga = font.get_glyph(ord("A"))
    if ga is None:
        print("    ERROR: 'A' not in preload")
        return 1
    u0, v0, u1, v1, gw, gh = ga
    print(f"    uv = ({u0:.4f},{v0:.4f}) - ({u1:.4f},{v1:.4f})  size={gw:.1f}x{gh:.1f}")
    if u1 <= u0 or v1 <= v0:
        print("    ERROR: degenerate UV rect")
        return 1

    print("[8] missing glyph returns None...")
    ghi = font.get_glyph(0xE000)  # Private Use Area — unlikely rasterised
    if ghi is not None:
        print(f"    NOTE: PUA U+E000 rasterised? shape={ghi}")

    print("[9] release_gpu()...")
    font.release_gpu()
    handle4 = font.ensure_texture(ctx)
    if handle4.id == 0 or handle4.id == handle.id:
        print(f"    ERROR: expected fresh non-zero handle, got {handle4.id} (prev={handle.id})")
        return 1
    print(f"    fresh handle after release: {handle4.id}")

    print("[cleanup] tearing down GL context")
    del holder
    video.SDL_GL_DeleteContext(gl_ctx)
    video.SDL_DestroyWindow(window)
    sdl2.SDL_Quit()
    print("FontAtlas smoke: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
