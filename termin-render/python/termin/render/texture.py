"""Simple 2D texture wrapper for the graphics backend."""

from __future__ import annotations

from pathlib import Path
from typing import Optional

import numpy as np

from termin.render.texture_asset import TextureAsset
from tgfx import TcTexture


class Texture:
    """
    Loads an image via Pillow and uploads it as ``GL_TEXTURE_2D``.

    This is a small Python wrapper over a ``TcTexture`` pool handle.
    """

    def __init__(self, path: Optional[str | Path] = None):
        self._asset: TextureAsset | None = None
        self._texture_data: TcTexture = TcTexture()
        if path is not None:
            self.load(path)

    @property
    def asset(self) -> TextureAsset | None:
        """Get underlying TextureAsset."""
        return self._asset

    @property
    def texture_data(self) -> TcTexture | None:
        """Get underlying TcTexture."""
        return self._texture_data if self._texture_data.is_valid else None

    @property
    def source_path(self) -> str | None:
        """Source path of the texture."""
        if self._asset is not None and self._asset.source_path is not None:
            return str(self._asset.source_path)
        source_path = self._texture_data.source_path
        return source_path or None

    @property
    def flip_x(self) -> bool:
        """Texture horizontal flip import flag."""
        td = self.texture_data
        return bool(td.flip_x) if td is not None else False

    @property
    def flip_y(self) -> bool:
        """Texture vertical flip import flag."""
        td = self.texture_data
        return bool(td.flip_y) if td is not None else True

    @property
    def transpose(self) -> bool:
        """Texture transpose import flag."""
        td = self.texture_data
        return bool(td.transpose) if td is not None else False

    @property
    def _size(self) -> tuple[int, int] | None:
        """Size of the texture (width, height)."""
        td = self.texture_data
        if td is not None:
            return (td.width, td.height)
        return None

    @property
    def _image_data(self) -> np.ndarray | None:
        """Raw image data (for preview)."""
        td = self.texture_data
        if td is not None:
            data = td.data
            if data is None:
                td.sync_to_cpu()
                data = td.data
            return data
        return None

    def load(self, path: str | Path) -> None:
        """Load texture from file."""
        asset = TextureAsset.from_file(path)
        self._asset = asset
        self._texture_data = asset.texture_data or TcTexture()

    def invalidate(self) -> None:
        """
        Invalidate cached GPU handles, forcing texture reload on next use.

        If source_path is set, reloads the texture from disk.
        """
        if self._asset is not None and self._asset.reload():
            self._texture_data = self._asset.texture_data or TcTexture()

    def bind(self, unit: int = 0) -> None:
        """Legacy immediate-GL bind hook. Rendering now binds through tgfx2."""
        _ = unit

    @classmethod
    def from_file(cls, path: str | Path) -> "Texture":
        """Create texture from file."""
        tex = cls()
        tex.load(path)
        return tex

    @classmethod
    def from_asset(cls, asset: TextureAsset) -> "Texture":
        """Create texture from existing TextureAsset."""
        tex = cls()
        tex._asset = asset
        pool_texture = TcTexture.from_uuid(asset.uuid)
        tex._texture_data = (
            pool_texture if pool_texture.is_valid else (asset.texture_data or TcTexture())
        )
        return tex

    @classmethod
    def from_data(
        cls,
        data: np.ndarray,
        width: int,
        height: int,
        source_path: str | None = None,
    ) -> "Texture":
        """
        Create texture from raw RGBA data.

        Args:
            data: Numpy array of shape (height, width, 4) with uint8 RGBA values.
            width: Texture width in pixels.
            height: Texture height in pixels.
            source_path: Optional source path for identification.

        Returns:
            Texture instance with the provided data.
        """
        texture_data = TcTexture.from_data(
            data=data,
            width=width,
            height=height,
            channels=4,
            flip_x=False,
            flip_y=True,
            transpose=False,
            name=source_path or "texture",
            source_path=source_path or "",
        )
        asset = TextureAsset(
            texture_data=texture_data,
            name=source_path or "texture",
            source_path=source_path,
            uuid=texture_data.uuid,
        )
        tex = cls()
        tex._asset = asset
        tex._texture_data = texture_data
        return tex


# --- White 1x1 Texture ---

_white_texture: Texture | None = None


def get_white_texture() -> Texture:
    """
    Returns a white 1x1 texture.

    Used as default for optional texture slots (like albedo when no texture is set).
    Singleton — created once.
    """
    global _white_texture

    if _white_texture is None:
        from termin.render.texture_handle import get_white_texture_handle

        texture_data = get_white_texture_handle()
        asset = TextureAsset(
            texture_data=texture_data,
            name="__white_1x1__",
            source_path="__white_1x1__",
            uuid=texture_data.uuid,
        )
        texture = Texture()
        texture._asset = asset
        texture._texture_data = texture_data
        _white_texture = texture

    return _white_texture


# --- Normal 1x1 Texture (flat normal) ---

_normal_texture: Texture | None = None


def get_normal_texture() -> Texture:
    """
    Returns a 1x1 normal map texture representing a flat surface (pointing up).

    RGB(128, 128, 255) = tangent space normal (0, 0, 1) after [0,255]->[-1,1] conversion.
    Used as default for normal map slots when no texture is set.
    Singleton — created once.
    """
    global _normal_texture

    if _normal_texture is None:
        from termin.render.texture_handle import get_normal_texture_handle

        texture_data = get_normal_texture_handle()
        asset = TextureAsset(
            texture_data=texture_data,
            name="__normal_1x1__",
            source_path="__normal_1x1__",
            uuid=texture_data.uuid,
        )
        texture = Texture()
        texture._asset = asset
        texture._texture_data = texture_data
        _normal_texture = texture

    return _normal_texture


# --- Dummy Shadow Texture (for sampler2DShadow) ---


class _DummyShadowTexture:
    """
    Placeholder depth texture for sampler2DShadow.

    AMD драйверы требуют чтобы sampler2DShadow были привязаны к
    валидным depth текстурам с GL_TEXTURE_COMPARE_MODE, даже если
    они не используются в шейдере.

    Возвращает 1.0 (полностью освещено) при любом сэмплировании.
    """

    def __init__(self):
        self._tex_id: int | None = None

    def _ensure_created(self):
        if self._tex_id is not None:
            return

        from OpenGL import GL as gl

        self._tex_id = gl.glGenTextures(1)
        gl.glBindTexture(gl.GL_TEXTURE_2D, self._tex_id)

        # 1x1 depth texture со значением 1.0 (максимальная глубина = нет тени)
        gl.glTexImage2D(
            gl.GL_TEXTURE_2D, 0, gl.GL_DEPTH_COMPONENT24,
            1, 1, 0,
            gl.GL_DEPTH_COMPONENT, gl.GL_FLOAT,
            np.array([1.0], dtype=np.float32)
        )

        # Параметры для sampler2DShadow
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_MIN_FILTER, gl.GL_NEAREST)
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_MAG_FILTER, gl.GL_NEAREST)
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_S, gl.GL_CLAMP_TO_EDGE)
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_T, gl.GL_CLAMP_TO_EDGE)

        # Hardware depth comparison для sampler2DShadow
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_COMPARE_MODE, gl.GL_COMPARE_REF_TO_TEXTURE)
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_COMPARE_FUNC, gl.GL_LEQUAL)

        gl.glBindTexture(gl.GL_TEXTURE_2D, 0)

    def bind(self, unit: int):
        """Bind to texture unit."""
        from OpenGL import GL as gl

        self._ensure_created()
        gl.glActiveTexture(gl.GL_TEXTURE0 + unit)
        gl.glBindTexture(gl.GL_TEXTURE_2D, self._tex_id)


_dummy_shadow_texture: _DummyShadowTexture | None = None


def get_dummy_shadow_texture() -> _DummyShadowTexture:
    """
    Returns a 1x1 depth texture for sampler2DShadow placeholders.

    Used when shadow maps are not available but shader expects
    sampler2DShadow to be bound (required by AMD drivers).
    """
    global _dummy_shadow_texture

    if _dummy_shadow_texture is None:
        _dummy_shadow_texture = _DummyShadowTexture()

    return _dummy_shadow_texture


def reset_dummy_shadow_texture() -> None:
    """
    Reset the global dummy shadow texture.

    Call this when OpenGL context changes (e.g., between tests)
    to ensure the texture is recreated in the new context.
    """
    global _dummy_shadow_texture
    _dummy_shadow_texture = None
