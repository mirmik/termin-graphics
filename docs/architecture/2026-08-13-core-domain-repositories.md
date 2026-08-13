# Core SDK And Domain Repository Boundary

Date: 2026-08-13

Status: accepted target architecture. The physical repository split is not yet
complete. The first implementation step is extraction of `termin-core`.

Implementation plan:

- [Termin Core repository extraction plan](../plans/2026-08-13-termin-core-repository-extraction.md)

Earlier analysis:

- [Repository and SDK split](../analysis/2026-08-07-repository-and-sdk-split.md)

The earlier analysis remains useful as an inventory, but its single
`termin-platform` ownership model is superseded by this decision. In particular,
the common foundation and Python runtime do not belong to the graphics domain.

## Decision

Termin is moving from one source repository towards a small shared Core SDK and
independent domain repositories:

```text
                         termin-core
                    Core SDK / ABI identity
                      /        |        \
                     v         v         v
            termin-graphics  termin-physics  termin-engine
             Graphics pack    Physics pack   Engine pack
                     \         |         /
                      \        |        /
                       deterministic composition
                                |
                                v
                     product SDKs and applications
```

The intended repositories are coarse product/domain boundaries, not one git
repository per library. `termin-core`, `termin-graphics`, `termin-physics` and
`termin-engine` are sufficient until a new independently releasable domain is
demonstrated by real consumers.

The physical boundary is part of the architecture:

- a domain consumes Core only through an installed, versioned Core SDK;
- engine consumes installed Core and domain packs;
- no repository may fall back to a sibling source checkout;
- a workspace repository may coordinate checkouts and commands, but may not
  expose source directories across product boundaries;
- a composed SDK is built in a new tree from immutable inputs rather than by
  installing one pack over another.

## Why Core must be separate from Graphics

The existing graphics profile is the most mature reusable product slice, but
that does not make graphics the owner of common process infrastructure. A future
physics repository needs logging, values, geometry, inspection, bindings and
possibly embedded Python without acquiring a dependency on GPU backends, shader
tools, UI or plotting.

If graphics owned `termin-base` and the canonical Python runtime, the apparent
dependency graph would be:

```text
termin-physics -> termin-graphics -> termin-base
```

That direction is semantically false and would make later physics extraction
repeat the same packaging migration. Core therefore becomes the first, smallest
repository split and the place where the installed-SDK contract is exercised.

## Core responsibility

Core owns only contracts which are useful to multiple independent domains and
do not import their semantics.

### Initial Core closure

The intended first closure is:

| Area | Initial ownership |
|---|---|
| Common C/C++ values and utilities | `termin-base` / `tcbase` |
| Caller-driven deferred execution | `termin-dispatch` |
| Generic type and inspection contracts | `termin-inspect` |
| Native Python binding ABI support | `termin-nanobind-sdk` |
| Embeddable canonical Python host | `termin-python-host` |
| Isolated SDK Python launcher | Python-only `termin-cli` target |
| Generic SDK manifest, lock and composition primitives | the domain-neutral subset of `termin-build-tools` |
| Shared process diagnostics | `termin-mcp` transport, session and executor runtime |

The machine-readable source of truth for the first closure is
`build-system/products/core.json`. It explicitly owns module roles, internal
dependencies, native targets, Python distributions, CMake packages, runtime
inputs, test suites and forbidden domain dependency roots. Build profiles and
the future extracted repository consume this product declaration rather than
maintaining a second hand-written Core list.

Core owns the canonical bundled Python runtime and its ABI identity. Domain
packs contribute wheels and native extensions, but do not ship competing Python
runtimes or bootstrap implementations. Native-only consumers may use the Core
CMake packages without embedding Python.

`termin-build-tools` must be divided by responsibility rather than copied
wholesale. Core owns reusable artifact schemas, provenance checks, locked Python
runtime installation and pack composition. Graphics, physics and engine own
their product closures, build recipes, smoke tests and application payload
rules.

### `termin-mcp` adapters

`termin-mcp` is part of the Core closure. It owns process-neutral JSON-RPC/MCP
transport, SDK-scoped session discovery and caller-driven Python execution.
The base distribution depends only on `tcbase`; it does not import scene,
graphics, assets, editor or player packages.

Domain facilities are contributed explicitly:

- editor and player hosts provide scene/runtime values such as
  `GeneralTransform3` through their executor adapters;
- `termin-graphics-mcp` owns texture/surface readback, NumPy conversion and PNG
  output under the `termin.graphics.mcp` namespace;
- editor/player packages choose concrete surfaces and expose product-specific
  tool schemas;
- the editor stdio broker remains editor-owned rather than joining Core.

### Explicit Core exclusions

Core does not own:

- image, mesh, GPU, shader, material, window or UI facilities;
- asset catalogs, UUID-based project resources, watchers or reload policy;
- scene/ECS, entities, prefabs or components;
- collision, physics, robotics or numerical domain algorithms;
- editor/player/runtime application hosts;
- domain shaders, icons, demo data or product-specific resource catalogs.

General usefulness alone is not sufficient for admission. A facility belongs
to Core only when at least two independent domains need the same contract and
the dependency remains domain-neutral.

## Graphics responsibility

`termin-graphics` consumes an installed Core SDK and owns the reusable visual
product:

- image and mesh data;
- GPU abstraction and backend implementations;
- shader compiler/runtime and the Termin shader ABI;
- materials and scene-neutral render core;
- window hosting;
- visual scene, native GUI, node graph and plotting;
- optional leaf format adapters such as a scene-neutral glTF document/visual
  integration;
- graphics showcase and Graphics SDK verification.

`termin-assets`, engine scene components and editor integration remain outside
this repository. glTF/GLB may be distributed with Graphics as an optional leaf
capability, but graphics and visual-scene foundations never depend on that
format.

## Physics responsibility

`termin-physics` will consume installed Core and own reusable computational
physics facilities:

- collision geometry/query core;
- rigid-body physics;
- optimization, robotics and multibody algorithms;
- FEM or related solvers when their implementation is scene-neutral;
- Python bindings, numerical tests and standalone examples.

Current physics is not yet cleanly extractable: `termin-physics` depends on
`termin-collision`, while `termin-collision` depends on `termin-scene` and
`termin-inspect`. The target split is:

```text
termin-physics: collision/physics computational core
termin-engine:  scene components, Entity transform synchronization,
                inspectors and debug-render adapters
```

Graphics debug rendering for physics is an integration package depending on
both Graphics and Physics; it is not part of either foundation.

## Engine responsibility

`termin-engine` is the integration and product layer. It owns:

- scene/ECS, components and prefab composition;
- assets and project resource management;
- adapters connecting scene entities to graphics and physics;
- engine render passes and lighting policy;
- editor, player, runtime bootstrap and project build/export;
- final product composition rules.

Engine may depend on Core, Graphics and Physics. None of those repositories may
depend on Engine.

## Artifact and compatibility model

Core produces a standalone install tree with headers, libraries, CMake package
configs, the canonical Python runtime, Core wheels, a wheelhouse and an artifact
manifest. The manifest records at least:

- artifact kind and schema version;
- target OS and architecture;
- configuration and compiler/runtime identity;
- Python ABI;
- Core build identity;
- file hashes and package provenance.

Each domain pack records an exact `requires_core_build_id` during active
development. ABI generations and version ranges may replace exact identity only
after the cross-repository ABI is deliberately stabilized.

```json
{
  "kind": "termin-graphics-pack",
  "graphics_build_id": "...",
  "requires_core_build_id": "...",
  "python_abi": "cp314t",
  "target": "linux-x86_64"
}
```

Composition verifies identity, target, Python ABI, hashes and path ownership;
rejects unexpected collisions; combines exact wheel locks; installs into a
fresh prefix; and writes a new manifest. It never overlays two pre-populated
`site-packages` trees.

Convenient published bundles remain possible:

- Termin Core SDK;
- Termin Graphics SDK = Core + Graphics;
- Termin Physics SDK = Core + Physics;
- Termin Full SDK = Core + Graphics + Physics + Engine.

These bundles are compositions of authoritative artifacts, not new source
ownership boundaries.

## Dependency and ownership enforcement

Repository separation is considered real only when CI proves:

1. Core builds and tests without graphics, physics, scene, assets or engine
   sources present.
2. Every domain builds against an installed Core SDK through package configs and
   declared Python artifacts.
3. Removing or renaming sibling source checkouts does not affect the build.
4. Public headers and installed resources contain no source-relative paths into
   another repository.
5. One file in a composed SDK has one owning pack, except for explicitly
   declared merge products such as the final manifest and resolved lock.
6. Reverse dependencies are rejected by repository-control checks.

## Migration order

The intended order is deliberately asymmetric:

1. Extract and publish Core.
2. Make the existing graphics product consume installed Core.
3. Extract Graphics and make Engine consume installed Core + Graphics.
4. Decouple collision/physics from scene and extract Physics.
5. Make Engine consume Core + Graphics + Physics.
6. Maintain an optional workspace only for coordinated development and
   cross-repository compatibility testing.

This order uses the smallest repository to establish the artifact boundary,
then applies the same mechanism to the already mature graphics profile. It does
not require physics cleanup to block the graphics split.

## Success criterion

The architecture is achieved when each domain repository can be checked out,
built, tested and distributed independently against pinned installed
dependencies, and the full Termin SDK/editor is reproduced solely by verified
artifact composition. No build requires source visibility across repository
boundaries.
