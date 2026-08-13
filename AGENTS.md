# AGENTS.md

## Scope

These instructions apply to the whole `termin-graphics` repository. Nested
`AGENTS.md` files add module-specific rules.

## Product boundary

This repository is one independently distributable Graphics SDK. It consumes
Core only through an absolute installed SDK path. Do not add sibling-checkout,
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
TERMIN_SLANGC=/absolute/path/to/slangc task smoke
```

Without Task, use `./build-sdk.sh --core-sdk ...`. There is no SDK profile
switch: Graphics is the product. Backend flags select capabilities within the
product.

The installed-consumer smoke is the release boundary. It must run against a
relocated SDK with source paths and ambient Python overlays removed.

## Engineering rules

Prefer simple, durable architecture over compatibility fallbacks. Do not use
`getattr`, `setattr`, or `hasattr` where ordinary typed access is intended, and
do not introduce C/C++ `thread_local` state. Failures must be logged or reported
explicitly.

Preserve unrelated work in a dirty tree. Use `apply_patch` for edits. If asked
to commit, do not add `Co-authored-by` trailers.
