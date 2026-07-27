# Termin Visual Scene

`termin-visual-scene` owns retained 2D item identity, topology and interaction.
It depends on the canonical geometry and drawing vocabulary in `termin-base`
and `termin-graphics`; those lower-level modules do not depend on it.

The C storage API uses per-scene index/generation handles. A handle includes
the scene lifetime-domain ID, so stale and cross-scene references are rejected.
Adopted payloads are destroyed exactly once by their optional custom deleter.
The C++ facade is move-only and does not use shared ownership for items.

All operations on a live scene are internally synchronized. A topology view is
a momentary snapshot and its payload pointer is borrowed; callers must provide
higher-level synchronization if they keep that pointer across mutations.
Destroying a scene requires exclusive lifetime ownership: no operation may race
with `tc_visual_scene_destroy`, and a payload deleter must not destroy its
owning scene recursively.
