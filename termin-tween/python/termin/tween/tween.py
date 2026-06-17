"""Base Tween class and transform tweens."""

from __future__ import annotations

from abc import ABC, abstractmethod
from enum import Enum, auto
from typing import Callable, TYPE_CHECKING

import numpy as np

from termin.tween.ease import Ease, evaluate as ease_evaluate

if TYPE_CHECKING:
    from termin.kinematic.general_transform import GeneralTransform3


class TweenState(Enum):
    """Tween lifecycle state."""

    RUNNING = auto()
    PAUSED = auto()
    COMPLETED = auto()
    KILLED = auto()


class Tween(ABC):
    """
    Base class for all tweens.

    Subclasses must implement:
    - _apply(t: float): Apply interpolated value at normalized time t (0..1)
    """

    def __init__(
        self,
        duration: float,
        ease: Ease = Ease.LINEAR,
        delay: float = 0.0,
    ):
        self.duration = duration
        self.ease = ease
        self.delay = delay

        self._elapsed: float = 0.0
        self._state: TweenState = TweenState.RUNNING
        self._on_complete: Callable[[], None] | None = None
        self._on_update: Callable[[float], None] | None = None

    @property
    def state(self) -> TweenState:
        return self._state

    @property
    def is_alive(self) -> bool:
        """True if tween is running or paused."""
        return self._state in (TweenState.RUNNING, TweenState.PAUSED)

    @property
    def is_complete(self) -> bool:
        return self._state == TweenState.COMPLETED

    def pause(self) -> "Tween":
        """Pause the tween."""
        if self._state == TweenState.RUNNING:
            self._state = TweenState.PAUSED
        return self

    def resume(self) -> "Tween":
        """Resume paused tween."""
        if self._state == TweenState.PAUSED:
            self._state = TweenState.RUNNING
        return self

    def kill(self) -> "Tween":
        """Kill the tween immediately without completing."""
        self._state = TweenState.KILLED
        return self

    def on_complete(self, callback: Callable[[], None]) -> "Tween":
        """Set callback to invoke when tween completes."""
        self._on_complete = callback
        return self

    def on_update(self, callback: Callable[[float], None]) -> "Tween":
        """Set callback to invoke on each update with current t value."""
        self._on_update = callback
        return self

    def update(self, dt: float) -> bool:
        """
        Update tween by dt seconds.

        Returns:
            True if tween is still alive, False if completed or killed.
        """
        if self._state != TweenState.RUNNING:
            return self._state == TweenState.PAUSED

        self._elapsed += dt

        # Handle delay
        if self._elapsed < self.delay:
            return True

        # Calculate progress
        active_time = self._elapsed - self.delay
        raw_t = min(1.0, active_time / self.duration) if self.duration > 0 else 1.0
        eased_t = ease_evaluate(self.ease, raw_t)

        # Apply the tween
        self._apply(eased_t)

        if self._on_update is not None:
            self._on_update(eased_t)

        # Check completion
        if raw_t >= 1.0:
            self._state = TweenState.COMPLETED
            if self._on_complete is not None:
                self._on_complete()
            return False

        return True

    @abstractmethod
    def _apply(self, t: float) -> None:
        """Apply interpolated value at normalized time t (0..1)."""
        pass


class MoveTween(Tween):
    """Tween for animating transform position."""

    def __init__(
        self,
        transform: "GeneralTransform3",
        target: np.ndarray,
        duration: float,
        ease: Ease = Ease.LINEAR,
        delay: float = 0.0,
    ):
        super().__init__(duration, ease, delay)
        self.transform = transform
        self.target = np.asarray(target, dtype=np.float32)
        self._start: np.ndarray | None = None

    def _apply(self, t: float) -> None:
        if self._start is None:
            self._start = self.transform.local_pose().lin.copy()

        new_pos = self._start + (self.target - self._start) * t
        pose = self.transform.local_pose()
        pose.lin = new_pos
        self.transform.relocate(pose)


class RotateTween(Tween):
    """Tween for animating transform rotation (quaternion slerp)."""

    def __init__(
        self,
        transform: "GeneralTransform3",
        target: np.ndarray,
        duration: float,
        ease: Ease = Ease.LINEAR,
        delay: float = 0.0,
    ):
        super().__init__(duration, ease, delay)
        self.transform = transform
        self.target = np.asarray(target, dtype=np.float32)
        self._start: np.ndarray | None = None

    def _apply(self, t: float) -> None:
        if self._start is None:
            self._start = self.transform.local_pose().ang.copy()

        new_rot = self._slerp(self._start, self.target, t)
        pose = self.transform.local_pose()
        pose.ang = new_rot
        self.transform.relocate(pose)

    @staticmethod
    def _slerp(q0: np.ndarray, q1: np.ndarray, t: float) -> np.ndarray:
        """Spherical linear interpolation between quaternions."""
        # Ensure shortest path
        dot = float(np.dot(q0, q1))
        if dot < 0:
            q1 = -q1
            dot = -dot

        # If very close, use linear interpolation
        if dot > 0.9995:
            result = q0 + t * (q1 - q0)
            return result / np.linalg.norm(result)

        theta_0 = np.arccos(dot)
        theta = theta_0 * t
        sin_theta = np.sin(theta)
        sin_theta_0 = np.sin(theta_0)

        s0 = np.cos(theta) - dot * sin_theta / sin_theta_0
        s1 = sin_theta / sin_theta_0

        result = s0 * q0 + s1 * q1
        return result / np.linalg.norm(result)


class ScaleTween(Tween):
    """Tween for animating transform scale."""

    def __init__(
        self,
        transform: "GeneralTransform3",
        target: np.ndarray | float,
        duration: float,
        ease: Ease = Ease.LINEAR,
        delay: float = 0.0,
    ):
        super().__init__(duration, ease, delay)
        self.transform = transform
        if isinstance(target, (int, float)):
            self.target = np.array([target, target, target], dtype=np.float32)
        else:
            self.target = np.asarray(target, dtype=np.float32)
        self._start: np.ndarray | None = None

    def _apply(self, t: float) -> None:
        if self._start is None:
            self._start = self.transform.local_pose().scale.copy()

        new_scale = self._start + (self.target - self._start) * t
        pose = self.transform.local_pose()
        pose.scale = new_scale
        self.transform.relocate(pose)
