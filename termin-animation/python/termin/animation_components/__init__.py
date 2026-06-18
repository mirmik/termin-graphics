"""Canonical animation component API.

The `_components_animation_native` binding is built by
termin-components/termin-components-animation and shipped inside the
termin-animation pip package (both contribute to the `termin.animation`
namespace). Since this wrapper lives in the separate
`termin.animation_components` namespace purely for API grouping, it
re-exports AnimationPlayer from where the binding actually lives.
"""

from termin.animation._components_animation_native import AnimationPlayer

__all__ = ["AnimationPlayer"]
