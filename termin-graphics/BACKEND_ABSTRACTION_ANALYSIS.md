# Backend Abstraction Analysis: OpenGL Coupling and Migration Plan

## Purpose
This document summarizes how strongly the current codebase is coupled to OpenGL and outlines a practical migration path toward supporting alternative APIs (Vulkan/Metal) without breaking the existing OpenGL path.

## Executive Summary
The project already has two useful abstraction points:
- C++ interface: `GraphicsBackend`
- C vtable: `tgfx_gpu_ops`

However, both abstractions are currently shaped by OpenGL concepts and do not fully isolate backend-specific semantics. The codebase is therefore **OpenGL-first**, not truly backend-agnostic.

Adding Vulkan/Metal is feasible, but it requires a non-trivial refactor of shared API/contracts and GPU resource model, not just “adding a new backend class”.

---

## Evidence: Where OpenGL leaks through the API

### 1. Public backend interface contains GL-specific methods and terms
File: `include/tgfx/graphics_backend.hpp`

Key issues:
- `reset_gl_state()`
- `check_gl_error(...)`
- `clear_gl_errors()`
- `get_bound_texture(...)`
- framebuffer methods using `fbo_id`

Why this is a leak:
- These are state-machine/debug operations specific to OpenGL.
- Vulkan/Metal do not expose equivalent runtime state querying patterns.
- This forces non-GL backends either to emulate GL semantics or return degraded behavior.

### 2. Shared handle interfaces expose OpenGL object model
File: `include/tgfx/handles.hpp`

Key issues:
- `FramebufferHandle::get_fbo_id()`
- `set_external_target(uint32_t fbo_id, ...)`
- `get_actual_gl_format/width/height/samples/filter`

Why this is a leak:
- FBO ID is an OpenGL object identity concept.
- Backend-neutral handles should not expose API-specific object IDs.

### 3. Core C GPU resource state stores OpenGL IDs directly
Files:
- `include/tgfx/tc_gpu_share_group.h`
- `include/tgfx/tc_gpu_context.h`
- `src/tgfx_resource_gpu.c`

Key issues:
- `tc_gpu_slot { uint32_t gl_id; ... }`
- mesh storage split into `vbo/ebo` + per-context `vao`
- flow in `tc_mesh_upload_gpu` is explicitly “shared VBO/EBO + per-context VAO recreation”

Why this is a leak:
- This data model is optimized for OpenGL context/VAO behavior.
- Vulkan/Metal resource lifetime/binding model is fundamentally different.

### 4. `tgfx_gpu_ops` is nominally abstract but semantically GL-shaped
File: `include/tgfx/tgfx_gpu_ops.h`

Key issues:
- Function signatures and comments refer to `VAO`, `VBO`, `EBO`, `glDrawElements`.
- Shader API is uniform-name based (`shader_set_*` by string) and program-centric.

Why this is a leak:
- Vulkan/Metal prefer pipeline layouts and descriptor/argument bindings, not ad-hoc uniform lookup by name at runtime.

### 5. Build and bindings are hardwired to OpenGL
Files:
- `CMakeLists.txt`
- `python/bindings/graphics_bindings.cpp`

Key issues:
- `find_package(OpenGL REQUIRED)` and GLAD linked into main target.
- Python API exposes `OpenGLGraphicsBackend` singleton directly.
- Functions like `init_opengl` are in public Python surface.

Why this is a leak:
- Runtime backend selection is structurally absent.
- Public API surface suggests OpenGL is the only first-class backend.

### 6. Data defaults assume OpenGL conventions
File: `src/resources/tc_texture_registry.c`

Key issue:
- `tex->flip_y = 1;  // Default for OpenGL`

Why this is a leak:
- Coordinate/origin policy should be explicit and backend-independent or explicitly normalized in shader/material pipeline.

---

## What is already useful (can be reused)

1. `GraphicsBackend` exists as a central interface.
2. `tgfx_gpu_ops` gives a C bridge that can remain as a backend-owned callback table.
3. Resource registries (`tc_mesh`, `tc_texture`, etc.) are already conceptually separate from concrete backend classes.

These are good foundations for staged migration.

---

## Target Architecture (Backend-Agnostic)

## Core design principles
1. Shared/public interfaces must describe rendering intent, not OpenGL internals.
2. All API-specific object IDs stay in backend-private code.
3. Shader/resource binding is slot/layout based, not string-uniform immediate mode.
4. Debug tooling is exposed via optional backend-specific extensions.

## High-level shape
- Core API:
  - `IRenderDevice`
  - `ICommandList`
  - backend-neutral resource handles (`BufferHandle`, `TextureHandle`, `PipelineHandle`, `RenderTargetHandle`)
- Backends:
  - OpenGL backend (current behavior preserved)
  - Vulkan backend
  - Metal backend
- Optional extension interfaces:
  - `IOpenGLDebugExtension`, `IVulkanDebugExtension`, etc.

---

## Recommended Migration Plan (Phased)

## Phase 0: Stabilize and isolate current GL behavior
Goal: no behavior change, only prepare separation.

Steps:
1. Mark GL-specific methods in shared interfaces as deprecated (documentation + comments).
2. Move OpenGL-only debug helpers behind a dedicated extension API.
3. Keep existing OpenGL code path fully operational.

Deliverables:
- No functional change.
- Clear contract boundaries documented.

## Phase 1: Remove GL terms from public/shared contracts
Goal: make interfaces backend-neutral.

Steps:
1. Replace or deprecate:
   - `reset_gl_state` -> `reset_state_cache` (or remove)
   - `check_gl_error/clear_gl_errors` -> generic debug diagnostics API
2. Remove `get_fbo_id` from generic `FramebufferHandle` contract.
3. Remove `get_actual_gl_*` from generic handle interface; move to GL extension.

Deliverables:
- Shared interfaces no longer mention GL explicitly.

## Phase 2: Redesign core GPU resource slots
Goal: eliminate `gl_id/vao/vbo/ebo` from shared C structs.

Steps:
1. Replace `tc_gpu_slot.gl_id` with opaque backend resource token.
2. Replace shared mesh representation with backend-neutral metadata + backend-private payload.
3. Move VAO/VBO/EBO tracking into OpenGL backend-private structures.

Deliverables:
- `tc_gpu_context` / `tc_gpu_share_group` no longer encode OpenGL object model.

## Phase 3: Evolve `tgfx_gpu_ops` to neutral operations
Goal: represent backend capabilities, not GL object choreography.

Steps:
1. Shift from GL-shaped calls (`mesh_upload -> VAO`) to neutral create/bind/draw interfaces.
2. Introduce explicit binding model for textures/buffers/samplers.
3. Keep a compatibility adapter so old OpenGL implementation still works during migration.

Deliverables:
- New ops table usable by both OpenGL adapter and future Vulkan/Metal backends.

## Phase 4: Shader and material model refactor
Goal: remove hard dependency on runtime uniform-by-name.

Steps:
1. Introduce resource binding slots (set/binding or equivalent abstraction).
2. Define shader reflection metadata layer.
3. Keep legacy uniform-name API as compatibility shim on top of new path (initially OpenGL-only if needed).

Deliverables:
- Portable material/shader binding model across OpenGL/Vulkan/Metal.

## Phase 5: Backend selection and modular build
Goal: make backend pluggable at build/runtime.

Steps:
1. Introduce backend factory (instead of only `OpenGLGraphicsBackend::get_instance`).
2. Split CMake targets:
   - `termin_graphics_core`
   - `termin_graphics_backend_opengl`
   - optional `..._vulkan`, `..._metal`
3. Update Python bindings to expose backend-neutral entry point + backend-specific modules.

Deliverables:
- Multiple backends can coexist without polluting core API.

## Phase 6: Add Vulkan/Metal incrementally
Goal: first functional non-GL backend.

Steps:
1. Implement minimum viable backend features needed by current test/demo path.
2. Add conformance tests running against OpenGL + new backend.
3. Expand feature parity iteratively (framebuffers, timer queries, advanced formats, etc.).

Deliverables:
- First alternative backend running real workloads.

---

## Risk & Complexity Assessment

Estimated complexity: **high**.

Primary risks:
1. Hidden OpenGL assumptions in higher-level code and Python bindings.
2. Shader pipeline mismatch (GLSL uniform-by-name vs descriptor-based APIs).
3. Resource lifetime synchronization differences (especially around context-sharing assumptions).
4. Potential temporary API churn for downstream users.

Mitigation:
- Keep OpenGL compatibility adapter throughout migration.
- Migrate in phases with strict no-regression checkpoints.
- Add backend-agnostic tests early (resource lifetime, draw correctness, readback).

---

## Suggested Immediate Next Actions

1. Approve Phase 1 interface cleanup scope (exact methods to deprecate/replace).
2. Introduce a small `BackendCapabilities` struct in core to make constraints explicit.
3. Add an architectural test/doc check that bans new `GL*` / `gl*` references in shared headers outside `include/tgfx/opengl`.
4. Draft compatibility strategy for Python API (avoid breaking existing OpenGL users while adding neutral API).

---

## Bottom line
Current architecture contains useful abstraction scaffolding, but OpenGL semantics are still embedded in shared contracts and core resource state. Supporting Vulkan/Metal is realistic, but requires systematic decoupling of interfaces and data models before implementing new backends.
