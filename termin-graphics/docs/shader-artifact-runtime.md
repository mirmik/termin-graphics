# Shader artifact runtime

Shader artifact resolution is scoped to a `tgfx::GraphicsHost` and its render
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

Artifact identity is selected with `tgfx::ShaderArtifactTarget`, independently
from `BackendType`. This matters for the GL family: modern desktop OpenGL keeps
the compatible `shaders/opengl/` layout, while OpenGL 3.3 and WebGL2 use the
distinct `shaders/opengl330/` and `shaders/webgl2/` roots. A render device may
therefore report the OpenGL backend while selecting the exact offline shader
profile through its capabilities. Generation of those constrained artifacts is
a separate build-pipeline concern; the runtime must never rewrite modern GLSL
into an older dialect. `termin_shaderc` generates both constrained profiles
from Slang SPIR-V through the repository-pinned SPIRV-Cross dependency:

```text
Slang source -> SPIR-V 1.3 -> GLSL 330 core
                           -> GLSL ES 300
```

SPIRV-Cross owns language lowering and combined image/sampler generation. The
compiler rejects cross-compilation failures and still emits the ordinary
reflection layout sidecar beside every artifact.

Standalone C++ applications that create a `GraphicsHost` directly can call
`tgfx::configure_default_standalone_shader_runtime`. It discovers
`termin_shaderc` and `slangc` from explicit environment settings, the active
SDK/build tree, or `PATH`, then configures a host-scoped resolver backed by the
platform user cache. `TERMIN_SDK_SHADER_CACHE_ROOT` overrides that cache base.

The old process-global tgfx2 setters remain as a compatibility boundary for
standalone graphics tools and tests that do not own a `RenderEngine`. Engine,
editor and packaged runtime paths must not use them.
