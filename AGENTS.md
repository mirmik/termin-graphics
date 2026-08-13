# AGENTS.md

## Scope

These instructions apply to the whole `termin-graphics` repository. Nested
`AGENTS.md` files add module-specific rules.

## Product boundary

This repository is one independently distributable Graphics SDK layer. It
contains only Graphics-owned artifacts and is composed over a matching Core
SDK for final distribution. It consumes Core only through an absolute installed SDK path. Do not add sibling-checkout,
`FetchContent`, source-overlay, or environment-guessing fallbacks for Core.

Graphics owns image, mesh, GPU/backends, shaders, materials, render core,
windowing, visual scene, native GUI, nodegraph, plotting, graphics MCP,
skeleton, animation, and scene-neutral GLB support. It does not own
`termin-assets`, engine scene/ECS/components, physics, editor/player, project
management, or application bootstrap.

## Build and verification

On Linux, use the public entry points:

```bash
task build -- --core-sdk /absolute/path/to/termin-core/sdk
task compose -- --core-sdk /absolute/path/to/termin-core/sdk
TERMIN_SLANGC=/absolute/path/to/slangc task smoke -- --core-sdk /absolute/path/to/termin-core/sdk
```

`Taskfile.yml` is the only public command surface. Repository scripts are
implementation details under `scripts/`; do not add root-level command
wrappers. There is no SDK profile switch: Graphics is the product. Backend
flags select capabilities within the product.

`task build` publishes the authoritative thin layer in `sdk/`; `task compose`
materializes the disposable runnable composition in `sdk-complete/`. Never add
Core-owned files to the layer merely to make it directly runnable.

The installed-consumer smoke is the release boundary. It must relocate Core
and the Graphics layer independently, compose them without file collisions,
and run with source paths and ambient Python overlays removed.

## Engineering rules

Prefer simple, durable architecture over compatibility fallbacks. Do not use
`getattr`, `setattr`, or `hasattr` where ordinary typed access is intended, and
do not introduce C/C++ `thread_local` state. Failures must be logged or reported
explicitly.

Preserve unrelated work in a dirty tree. Use `apply_patch` for edits. If asked
to commit, do not add `Co-authored-by` trailers.
