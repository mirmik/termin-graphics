# Termin Graphics

Termin Graphics is the independently buildable visual SDK from Termin. It
contains the GPU abstraction and desktop backends, shader compiler/runtime,
materials, image and mesh facilities, visual scene, native GUI, plotting, and
scene-neutral GLB, skeleton, and animation support.

The repository consumes an **installed Termin Core SDK**. It does not consume a
Core source checkout and does not own assets, engine scene/ECS, editor, player,
or product bootstrap code.

## Build

Initialize submodules, build `termin-core`, then pass its absolute installed SDK
path:

```bash
git submodule update --init --recursive
task build -- --core-sdk /absolute/path/to/termin-core/sdk
```

`termin-graphics` is one product, so there is no `--profile graphics` switch.
Vulkan, OpenGL, and SDL remain optional backend features (`--no-vulkan`,
`--no-opengl`, `--no-sdl`). The output in `sdk/` is a thin Graphics layer: it
contains no Core runtime, libraries, headers, or CMake packages. Its
`sdk-inputs.json` records the exact Core input identity. A distributable or
runnable SDK is formed by composing this layer over that immutable Core SDK;
file collisions are rejected.

## Verify the installed boundary

The smoke test relocates Core and Graphics independently, composes them in a
temporary directory, hides this checkout, builds a native CMake consumer,
imports the isolated Python API, compiles shaders and materials, and runs the
headless animated/skinned GLB showcase:

```bash
TERMIN_SLANGC=/absolute/path/to/slangc task smoke -- \
  --core-sdk /absolute/path/to/termin-core/sdk
```

The product boundary and extraction stages are documented in
[`docs/plans/2026-08-13-termin-graphics-repository-extraction.md`](docs/plans/2026-08-13-termin-graphics-repository-extraction.md).
