# Termin Graphics Repository Extraction Plan

Date: 2026-08-13

Canonical architecture:

- [Core SDK and domain repository boundary](../architecture/2026-08-13-core-domain-repositories.md)

Taskboard:

- #1619 - extraction umbrella;
- #1625 - GLB, skeleton and animation domain/adapters split;
- #1620 - canonical Graphics product closure;
- #1621 - standalone Graphics SDK verification;
- #1622 - physical repository extraction;
- #1623 - installed Graphics consumption from Termin;
- #1624 - duplicated-source removal and ownership cutover.

## Goal

Create an independently buildable and distributable `termin-graphics`
repository over the installed Termin Core SDK. The repository must provide a
complete reusable visual stack, including GLB models and skeletal animation,
without depending on the editor, `termin-assets` or the engine scene/ECS.

Termin then consumes pinned Core and Graphics SDK artifacts and retains only
engine-specific integration.

## Product boundary

Graphics owns:

- image and mesh data;
- GPU abstraction and Vulkan, OpenGL, D3D11 and WebGPU backends;
- shader compilation, runtime, reflection and Termin shader ABI;
- materials and scene-neutral rendering;
- window hosting;
- visual scene, native GUI, node graph and plotting;
- graphics-owned MCP inspection and readback;
- skeleton data and scene-neutral skeletal runtime;
- animation clips and scene-neutral playback runtime;
- GLB/glTF decoding, imported visual data and presentation of static, skinned
  and animated models;
- standalone showcase, tests and SDK product recipes.

Graphics consumes Core through installed CMake packages and Python
distributions. Core modules are external inputs, never Graphics-owned modules.

Termin retains:

- `termin-assets`, resource-manager plugins and asset persistence;
- engine scene/ECS and component adapters;
- default application assets and engine render policy;
- editor, player, bootstrap and other product hosts.

The current packages do not yet honor this boundary. In particular,
`termin-skeleton` reaches into scene/assets, `termin-animation` exposes asset
wrappers, and high-level `termin-glb` combines document loading with assets,
default assets and Entity/component instantiation. These are migration inputs,
not reasons to move the upper layers into Graphics.

## Stage 0: freeze the target boundary

The ownership list above is the migration invariant. Inventory the current
native/Python dependency graph and reject any proposal that solves a reverse
dependency by moving assets, scene/ECS or product code into Graphics.

Exit criteria:

- every candidate path is classified as portable Graphics code or a
  consumer-side adapter;
- GLB, skeleton and animation are explicitly included in Graphics;
- the concrete leaks to remove are recorded in #1625.

## Stage 1: separate portable domains from Termin adapters

Refactor GLB, skeleton and animation by responsibility:

- portable data structures, registries and bindings remain Graphics-owned;
- GLB decoding produces scene-neutral mesh, material, skeleton and animation
  data;
- presentation targets a visual/render abstraction rather than engine Entity
  construction;
- asset wrappers/plugins move to Termin-owned adapter packages;
- Entity/component synchronization and runtime repair remain engine adapters.

Do not retain optional import fallbacks that conceal a reversed dependency. A
Graphics package must be installable and importable with no engine packages
present.

Exit criteria:

- Graphics-owned CMake and Python metadata have no assets/scene/component edge;
- neutral and adapter tests cover both sides of the seam;
- Termin behavior remains available through explicit consumer adapters.

## Stage 2: declare the canonical product closure

Add one machine-readable Graphics product manifest. It is authoritative for
owned modules, native targets, Python distributions, CMake packages, tests,
resources and forbidden reverse dependencies.

The manifest model must distinguish owned internal dependencies from installed
external product dependencies. The Graphics profile is projected from this
manifest rather than maintained as an independent package list.

Exit criteria:

- every Graphics-owned module has one manifest entry;
- installed Core dependencies are explicit external edges;
- assets, engine scene/ECS and product hosts are rejected;
- GLB, skeleton and animation are present in the validated closure;
- repository validation detects undeclared and reversed dependencies.

## Stage 3: prove a standalone Graphics SDK

Build and install the closure from the current repository against only a pinned
installed Core SDK. Verification runs outside the source checkout and clears
source overlays.

Required consumers include:

1. a native CMake renderer consumer using installed packages;
2. isolated Python imports from the composed Core + Graphics runtime;
3. shader compilation and material-runtime smoke coverage;
4. a showcase loading and presenting an animated skinned GLB;
5. graphics MCP inspection/readback where the selected backend supports it.

Exit criteria:

- clean Linux build, install and source-hidden consumers pass;
- an installed manifest records exact Core compatibility identity;
- removal of any required installed input fails loudly;
- no source-relative public path or ambient monorepo import remains.

## Stage 4: extract the physical repository

Filter the proven Graphics-owned paths into `termin-graphics`, preserving
useful history. Give it repository-local build/test entry points, product
manifest, runtime additions, CI and artifact publication.

The repository build has no `TERMIN_SDK_PROFILE` switch: Graphics is the
product. Optional platform backends remain feature selections within that
product, not alternate ownership profiles.

Exit criteria:

- a Graphics-only checkout builds against an installed Core SDK;
- Linux CI publishes a verified, relocatable artifact with exact identity;
- platform jobs cover the supported backend matrix;
- no Termin source checkout is fetched or mounted.

## Stage 5: consume installed Graphics from Termin

Compose Termin from immutable Core and Graphics inputs. A Graphics SDK path is
required and its build identity is inferred and verified from its manifest;
an explicit identity may only be an expectation check.

Use installed CMake configs and wheels without sibling checkout, FetchContent,
environment guessing or source fallback. Add a source-hidden job which removes
all Graphics-owned directories before configuring Termin.

Exit criteria:

- full Termin SDK/editor builds with Graphics sources absent;
- composed artifacts retain one owner per file;
- Core/Graphics identity, platform and Python ABI mismatches fail at compose
  time;
- Linux source-hidden verification passes.

## Stage 6: cut over ownership

After installed consumption is proven, remove duplicated Graphics source,
tests and repository-control ownership from Termin in one clean migration.
Keep consumer adapters and integration tests in Termin.

Cross-repository API changes land Graphics first, publish an identifiable SDK,
then update the pinned Termin consumer. Temporary compatibility may be
additive, but duplicated source or implicit sibling fallbacks must not return.

Exit criteria:

- `termin-graphics` is the sole source owner of the declared closure;
- Termin builds from a fresh checkout with installed Core and Graphics inputs;
- standalone GLB/skeleton/animation showcase and Termin integration both pass;
- the previous combined revision remains the explicit rollback point.
