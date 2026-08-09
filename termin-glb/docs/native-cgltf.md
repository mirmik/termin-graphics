# cgltf dependency

The native GLB backend uses cgltf from:

```text
upstream: https://github.com/jkuhlmann/cgltf.git
revision: 85cd62382dfea638278962690cf515023f33ed00
cgltf version: 1.15
license: MIT
```

`termin-thirdparty/cgltf` is a pinned Git submodule. Exactly one Termin
translation unit, `termin-glb/src/cgltf_impl.cpp`, defines
`CGLTF_IMPLEMENTATION`. Engine-specific resource construction, coordinate
conversion, diagnostics, and extension policy belong to the `termin-glb`
adapter rather than to cgltf.

The submodule URL may move to a Termin-owned fork when a parser fix is needed.
Every fork-only patch must remain small, have a regression test, document its
upstream base revision and rationale, and be suitable for upstream submission.
Changing the remote must not change the `termin-glb` adapter API.

## Static mesh import contract

`NativeStaticMeshDocument` is the explicit migration boundary for binary GLB
files. Construction maps the file read-only, parses it with cgltf, loads the
embedded BIN chunk as a view of that mapping, and exposes only compact mesh
discovery records to Python. The mapping and `cgltf_data` remain alive until
the document object is destroyed.

Import is deliberately two-phase:

1. Read `document.meshes` and resolve or create the corresponding embedded
   mesh asset UUIDs.
2. Call `build_mesh(mesh_index, uuid, ...)` for each selected mesh. The native
   backend allocates a `tc_mesh_data_builder`, unpacks accessors directly into
   its final interleaved storage, and atomically commits the complete payload
   to that UUID.

The static path supports triangle primitives, `POSITION`, `NORMAL`,
`TEXCOORD_0`, and `TANGENT`, sparse and normalized accessors, accessor stride,
U8/U16/U32 indices, and non-indexed primitives. Multiple primitives become
submeshes with stable per-mesh material slots. The smallest compatible existing
Termin layout is selected; missing fields in a required superset are
zero-filled. Coordinate conversion is
the explicit `(x, y, z) -> (x, -z, y)` option and applies to positions,
normals, and tangent xyz while preserving tangent handedness.

Unsupported topology and skinned attributes fail with structured contextual
errors; skinning belongs to the later rig migration. The native API never
falls back to the Python loader. JSON `.gltf` remains on the existing Python
path until its external-buffer and URI contract is migrated intentionally.

cgltf does not clamp the most-negative signed normalized integer after division
(`-128/127` or `-32768/32767`). The adapter clamps these values to `-1` as
required by glTF. This is intentionally local and tested; it does not justify
a Termin fork on its own.

## Reference-model verification

`test_glb_native_reference_models.py` verifies the two local Pixal3D models
named in the migration brief against stable native payload sizes and FNV-1a
hashes. Tests skip when the models are absent; CI or another workstation can
override their locations with `TERMIN_GLB_GEOMETRY_REFERENCE` and
`TERMIN_GLB_TEXTURED_REFERENCE`. Payload hashing is an opt-in diagnostic path
and is not performed by normal `build_mesh()` calls.

On the geometry-only reference, the native importer produces 2,823,922
vertices and 17,280,924 indices in a 12-byte position-only layout. The payload
is 103,010,760 bytes with hash `5462061671071880564`. A measured local run
opened the mapped document in 38.7 ms and built plus hashed it in 142.4 ms;
maximum process RSS was 256,608 KiB. Twelve repeated load/build/destroy cycles
stabilized at 51,948 KiB after warm-up (8 KiB tail growth).
