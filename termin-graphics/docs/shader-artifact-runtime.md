# Shader artifact runtime

Shader artifact resolution is scoped to a `tgfx::RenderRuntime` and its render
device. `termin::ShaderArtifactResolver` carries the artifact root, writable
cache root, compiler path and dev-compile policy. `RenderEngine` accepts this
configuration before or after lazy tgfx2 initialization and applies it to the
runtime device.

Runtime package loading is transactional with respect to shader resolution:
`RuntimePackageLoader` validates and loads a package, then returns a
`ShaderRuntimeConfiguration` in its result. The player, Android and OpenXR
composition roots explicitly apply that configuration to their `RenderEngine`.
The Python player follows the same rule. A failed package load therefore does
not mutate another runtime's artifact resolver.

Backend shader caches remain device-owned because compiled shader handles are
device resources. Every cache entry records the resolver revision. Reconfiguring
one device increments its resolver revision, so an old entry is discarded on
the next use instead of reusing a shader compiled from the previous root.

The old process-global tgfx2 setters remain as a compatibility boundary for
standalone graphics tools and tests that do not own a `RenderEngine`. Engine,
editor and packaged runtime paths must not use them.
