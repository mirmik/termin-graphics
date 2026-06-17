"""
Tween module - smooth parameter animation system.

Usage:
    from termin.tween import TweenManagerComponent, Ease

    # Add to scene
    tween_entity = Entity(name="TweenManager")
    tweens = TweenManagerComponent()
    tween_entity.add_component(tweens)
    scene.add(tween_entity)

    # Create tweens
    tweens.move(entity.transform, target_pos, 1.0, ease=Ease.OUT_QUAD)
    tweens.rotate(entity.transform, target_quat, 0.5, ease=Ease.IN_OUT_CUBIC)
    tweens.scale(entity.transform, 2.0, 0.3)

    # With callbacks
    tweens.move(entity.transform, pos, 1.0).on_complete(lambda: print("Done!"))

    # Chaining
    tweens.move(entity.transform, pos1, 1.0).on_complete(
        lambda: tweens.move(entity.transform, pos2, 1.0)
    )
"""

from pkgutil import extend_path

from termin.tween.ease import Ease
from termin.tween.tween import Tween, TweenState, MoveTween, RotateTween, ScaleTween
from termin.tween.manager import TweenManager

__path__ = extend_path(__path__, __name__)

__all__ = [
    # Easing
    "Ease",
    # Base classes
    "Tween",
    "TweenState",
    # Transform tweens
    "MoveTween",
    "RotateTween",
    "ScaleTween",
    # Manager
    "TweenManager",
    "TweenManagerComponent",
]


def __getattr__(name: str):
    if name == "TweenManagerComponent":
        from termin.tween.component import TweenManagerComponent

        return TweenManagerComponent
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
