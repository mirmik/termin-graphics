# GL compatibility tiers

Termin treats the GL API family as several explicit feature tiers. Backend
identity alone is not enough: desktop OpenGL can consume either modern GLSL or
GLSL 330, while WebGL2 consumes GLSL ES 300 and lacks several desktop entry
points.

| Tier | Artifact target | Clip control | Polygon mode | Base vertex | Multisample textures | Compute | Shadow samplers |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Modern` | `OpenGL450` (`shaders/opengl`) | required | capability | capability | capability | not currently advertised | 16 |
| `Constrained33` | `OpenGL330` | no | capability | capability | capability | no | 8 |
| `WebGL2` | `WebGL2` | no | no | no | no | no | 8 |

`derive_gl_feature_set` validates a requested tier against reported API,
version, entry points and limits. Shared rendering code consumes the resulting
feature set; it must not rediscover support through local version checks.

`gl_coordinate_contract` derives the matching coordinate boundary:

| Tier | Vertex Y | Vertex Z | Native front face |
| --- | --- | --- | --- |
| `Modern` | unchanged | unchanged `[0,1]` | inverted for `GL_UPPER_LEFT` |
| `Constrained33` | `-y` | `2z-w` to `[-1,1]` | direct |
| `WebGL2` | `-y` | `2z-w` to `[-1,1]` | direct |

Viewport, scissor and readback continue to expose top-left pixel coordinates;
the contract converts them once to GL's bottom-left framebuffer API. Rendered
and uploaded textures retain `v=0` as the visual top at the public boundary.

The shadow metadata UBO remains fixed at 16 entries on every backend so its
std140 offsets and CPU ABI do not vary. Constrained shader artifacts declare
only eight static comparison samplers, and runtime shadow upload/binding is
capped by `BackendCapabilities::max_shadow_maps`. This leaves at least eight
of OpenGL 3.3's guaranteed 16 fragment texture units available to materials.

The existing desktop device remains on `Modern`. A desktop application selects
the constrained tier before context creation with
`TERMIN_OPENGL_TIER=opengl33`; the window system then requests a 3.3 core
context and the device consumes `shaders/opengl330`. The startup log records
the selected tier, artifact target, actual API version, renderer, driver,
fragment texture limit and shadow budget. WebGL2 devices select their tier
explicitly during browser context creation. The browser host uses the same
`OpenGLRenderDevice`, command lists and render passes as desktop GL; only the
context owner, presentation to the canvas default framebuffer, static ES 3
entry-point bridge and declared feature set are platform-specific. Optional GL
operations belong behind the shared GL platform-operation boundary, with a
clear rejection log when a requested operation is unavailable.

GLSL 3.30 / GLSL ES 3.00 artifacts use symbolic uniform-block and sampler
names at runtime. Their sidecars contain compact per-program native binding
indices rather than the wider cross-backend logical slots. Cross-stage
varyings are normalized from SPIR-V locations to identical GLSL names because
these language versions cannot use modern inter-stage location qualifiers.

The SDK build compiles and installs `opengl330` built-ins whenever desktop
OpenGL is enabled. Runtime loading is offline: neither `slangc` nor
`termin_shaderc` is needed on the target machine. A development/build machine
still needs the locked Slang toolchain while producing the SDK.

The automated acceptance smoke runs a true 3.3 context, disables runtime
shader compilation, and pixel-checks Canvas primitives, top-left scissor,
sampled textures and Text2D. On Mesa CI it can be reproduced with:

```bash
xvfb-run -a env \
  MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 \
  TERMIN_BACKEND=opengl TERMIN_OPENGL_TIER=opengl33 \
  build/Release-tests/bin/termin_window_opengl33_tier
```

For physical legacy-GPU acceptance, omit both Mesa override variables. The
reported `OPENGL33_CONTEXT` must be `3.3`, `TARGET` must be `opengl330`, and
`SOFTWARE` must be `no`; preserve the renderer/driver line with the test
record. A failure to create 3.3, compile/link an offline shader, or satisfy a
resource budget is logged with its shader/stage/target identity.

## Platform-operation boundary

`GlPlatformOperations` is the only layer allowed to invoke the optional GL
entry points used by shared command and resource code:

- clip-control setup and validation;
- desktop polygon mode (WebGL2 `Fill` is an already-satisfied no-op);
- indexed base-vertex draw variants;
- multisample texture allocation;
- timestamp issue and result retrieval.

An unsupported operation returns `false` and writes an error to the shared
log. Callers must reject the resource, pipeline or draw instead of silently
substituting different rendering. The modern desktop path passes through the
same boundary, so compatibility work does not create a second command model.
