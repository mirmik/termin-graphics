"""TweenManager - manages and updates all active tweens."""

from __future__ import annotations

from collections.abc import Sequence
from typing import TYPE_CHECKING

from termin.tween.ease import Ease
from termin.tween.tween import Tween, MoveTween, RotateTween, ScaleTween

if TYPE_CHECKING:
    from termin.kinematic.general_transform import GeneralTransform3
    from termin.scene import Entity


class TweenManager:
    """
    Manages active tweens and provides factory methods.

    Usage:
        tweens = TweenManager()

        # Create tweens
        tweens.move(entity.transform, target_pos, 1.0, ease=Ease.OUT_QUAD)
        tweens.rotate(entity.transform, target_quat, 0.5)
        tweens.scale(entity.transform, 2.0, 0.3)

        # In game loop
        tweens.update(dt)
    """

    def __init__(self):
        self._tweens: list[Tween] = []

    def update(self, dt: float) -> None:
        """Update all active tweens. Removes completed/killed tweens."""
        alive = []
        for tween in self._tweens:
            if tween.update(dt):
                alive.append(tween)
        self._tweens = alive

    def add(self, tween: Tween) -> Tween:
        """Add a custom tween to the manager."""
        self._tweens.append(tween)
        return tween

    def move(
        self,
        transform: "GeneralTransform3",
        target: Sequence[float],
        duration: float,
        ease: Ease = Ease.LINEAR,
        delay: float = 0.0,
    ) -> MoveTween:
        """Create a position tween."""
        tween = MoveTween(transform, target, duration, ease, delay)
        self._tweens.append(tween)
        return tween

    def rotate(
        self,
        transform: "GeneralTransform3",
        target: Sequence[float],
        duration: float,
        ease: Ease = Ease.LINEAR,
        delay: float = 0.0,
    ) -> RotateTween:
        """Create a rotation tween (target is quaternion xyzw)."""
        tween = RotateTween(transform, target, duration, ease, delay)
        self._tweens.append(tween)
        return tween

    def scale(
        self,
        transform: "GeneralTransform3",
        target: Sequence[float] | float,
        duration: float,
        ease: Ease = Ease.LINEAR,
        delay: float = 0.0,
    ) -> ScaleTween:
        """Create a scale tween (target can be float for uniform or vec3)."""
        tween = ScaleTween(transform, target, duration, ease, delay)
        self._tweens.append(tween)
        return tween

    def kill_all(self, transform: "GeneralTransform3 | None" = None) -> int:
        """
        Kill all tweens, optionally filtered by transform.

        Args:
            transform: If provided, only kill tweens targeting this transform.

        Returns:
            Number of killed tweens.
        """
        killed = 0
        for tween in self._tweens:
            if transform is None:
                tween.kill()
                killed += 1
            elif isinstance(tween, (MoveTween, RotateTween, ScaleTween)):
                if tween.transform is transform:
                    tween.kill()
                    killed += 1
        return killed

    def kill_entity(self, entity: "Entity") -> int:
        """
        Kill all tweens targeting the entity's transform.

        Args:
            entity: Entity whose tweens should be killed.

        Returns:
            Number of killed tweens.
        """
        return self.kill_all(entity.transform)

    def pause_all(self, transform: "GeneralTransform3 | None" = None) -> int:
        """Pause all tweens, optionally filtered by transform."""
        paused = 0
        for tween in self._tweens:
            if transform is None:
                tween.pause()
                paused += 1
            elif isinstance(tween, (MoveTween, RotateTween, ScaleTween)):
                if tween.transform is transform:
                    tween.pause()
                    paused += 1
        return paused

    def resume_all(self, transform: "GeneralTransform3 | None" = None) -> int:
        """Resume all paused tweens, optionally filtered by transform."""
        resumed = 0
        for tween in self._tweens:
            if transform is None:
                tween.resume()
                resumed += 1
            elif isinstance(tween, (MoveTween, RotateTween, ScaleTween)):
                if tween.transform is transform:
                    tween.resume()
                    resumed += 1
        return resumed

    @property
    def count(self) -> int:
        """Number of active tweens."""
        return len(self._tweens)

    def clear(self) -> None:
        """Remove all tweens without calling callbacks."""
        for tween in self._tweens:
            tween.kill()
        self._tweens.clear()
