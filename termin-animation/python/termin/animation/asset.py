"""Animation clip asset model."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

from termin_assets import DataAsset

if TYPE_CHECKING:
    from termin.animation import TcAnimationClip


class AnimationClipAsset(DataAsset["TcAnimationClip"]):
    """
    Asset for animation clip data.

    IMPORTANT: Create through ResourceManager.get_or_create_animation_clip_asset(),
    not directly. This ensures proper registration and avoids duplicates.

    Stores TcAnimationClip (channels, duration, etc).
    """

    _uses_binary = False

    def __init__(
        self,
        clip: "TcAnimationClip | None" = None,
        name: str = "animation",
        source_path: Path | str | None = None,
        uuid: str | None = None,
    ):
        super().__init__(data=clip, name=name, source_path=source_path, uuid=uuid)

    @property
    def clip(self) -> "TcAnimationClip | None":
        """TcAnimationClip data."""
        return self.data

    @clip.setter
    def clip(self, value: "TcAnimationClip | None") -> None:
        """Set clip and bump version."""
        self.data = value

    @property
    def duration(self) -> float:
        """Animation duration in seconds."""
        data = self.data
        return data.duration if data else 0.0

    def _parse_content(self, content: str) -> "TcAnimationClip | None":
        """Parse animation content from file."""
        from termin.animation import parse_animation_content

        return parse_animation_content(content)

    @classmethod
    def from_clip(
        cls,
        clip: "TcAnimationClip",
        name: str | None = None,
        source_path: Path | str | None = None,
    ) -> "AnimationClipAsset":
        """Create AnimationClipAsset from existing TcAnimationClip."""
        asset_name = name or clip.name or "animation"
        return cls(clip=clip, name=asset_name, source_path=source_path)
