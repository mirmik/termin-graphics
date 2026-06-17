from __future__ import annotations

import numpy as np

from termin.tween import Ease, MoveTween, ScaleTween, TweenManager, TweenState
from termin.tween.ease import evaluate


class _Pose:
    def __init__(self) -> None:
        self.lin = np.array([0.0, 0.0, 0.0], dtype=np.float32)
        self.ang = np.array([0.0, 0.0, 0.0, 1.0], dtype=np.float32)
        self.scale = np.array([1.0, 1.0, 1.0], dtype=np.float32)


class _Transform:
    def __init__(self) -> None:
        self._pose = _Pose()

    def local_pose(self) -> _Pose:
        return self._pose

    def relocate(self, pose: _Pose) -> None:
        self._pose = pose


def test_ease_linear_evaluates_endpoints() -> None:
    assert evaluate(Ease.LINEAR, 0.0) == 0.0
    assert evaluate(Ease.LINEAR, 1.0) == 1.0


def test_move_tween_updates_transform_and_completes() -> None:
    transform = _Transform()
    tween = MoveTween(transform, np.array([2.0, 4.0, 6.0]), duration=2.0)

    assert tween.update(1.0) is True
    np.testing.assert_allclose(transform.local_pose().lin, [1.0, 2.0, 3.0])

    assert tween.update(1.0) is False
    np.testing.assert_allclose(transform.local_pose().lin, [2.0, 4.0, 6.0])
    assert tween.state == TweenState.COMPLETED


def test_manager_removes_completed_tweens() -> None:
    transform = _Transform()
    manager = TweenManager()
    manager.add(ScaleTween(transform, 3.0, duration=0.5))

    manager.update(0.5)

    assert manager.count == 0
    np.testing.assert_allclose(transform.local_pose().scale, [3.0, 3.0, 3.0])
