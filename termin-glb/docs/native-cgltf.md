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

`NativeGLBDocument` is the explicit migration boundary for binary GLB
files. Construction maps the file read-only, parses it with cgltf, loads the
embedded BIN chunk as a view of that mapping, and exposes only compact mesh
discovery records to Python. The mapping and `cgltf_data` remain alive until
the document object is destroyed. `NativeStaticMeshDocument` remains as the
stage-1 compatibility name.

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
Termin layout is selected. UV-bearing meshes use the PBR-compatible tangent
layout; missing tangents are generated from indexed positions, normals, and
UVs, and missing normals needed by that derivation are generated from triangle
faces. Other missing fields in a required superset are zero-filled. Coordinate conversion is
the explicit `(x, y, z) -> (x, -z, y)` option and applies to positions,
normals, and tangent xyz while preserving tangent handedness.

Unsupported topology and skinned attributes fail with structured contextual
errors. Skinned primitives use the existing 80-byte Termin layout and preserve
`JOINTS_0`/`WEIGHTS_0`; static UV meshes without authored tangents derive the
PBR tangent field during native unpacking. The native API never falls back to
the Python loader. JSON `.gltf` remains on the existing Python path until its
external-buffer and URI contract is migrated intentionally.

`GLBAsset` uses this path by default for binary `.glb`. The asset maps the
source GLB instead of
reading it into a Python byte string, discovers compact child-resource
metadata, and publishes meshes, skeletons, and animation clips directly from
the native document. Entity hierarchy and material application still use the
shared Python instantiator, but geometry and animation key tensors never make a
Python round trip. JSON `.gltf` remains on the Python loader because external
buffer and URI resolution is a distinct source-format contract. During the
final validation window, `import_backend: python` is an explicit legacy
override for `.glb`; selecting either backend never falls back silently after
an error.

## Materials and encoded images

The mapped document exposes separate compact image, texture, texture-view, and
material records. This deliberately does not reproduce the legacy
`GLBTcTexture` model inside the C ABI: image payload identity and texture
sampler identity remain distinct. Embedded image bytes stay in the mapped BIN
chunk and are copied only by an explicit `image_payload()` call.

`EXT_texture_webp` and `KHR_texture_basisu` select their extension image before
the core texture source. Effective sampler values use the existing Termin
compatibility defaults `(9729, 9729, 10497, 10497)`. Standard metallic-
roughness factors and base-color, metallic-roughness, normal, occlusion, and
emissive texture views preserve texture index, `texCoord`, and scale/strength.
The bridge into the existing Python texture/material layer passes encoded
PNG/JPEG/WebP/KTX2 bytes without decoding them in the cgltf adapter.

The current renderer consumes only `TEXCOORD_0`; another texture coordinate
set therefore fails explicitly during material configuration. Likewise,
`KHR_texture_transform` is preserved during discovery but rejected by the
legacy bridge instead of being silently ignored. Unknown required extensions
fail when the native document is opened. URI-backed images and JSON `.gltf`
remain outside this mapped-GLB stage.

cgltf does not clamp the most-negative signed normalized integer after division
(`-128/127` or `-32768/32767`). The adapter clamps these values to `-1` as
required by glTF. This is intentionally local and tested; it does not justify
a Termin fork on its own.

## Reference-model verification

`test_glb_native_reference_models.py` verifies the two local Pixal3D models
named in the migration brief against stable geometry, image, material, and
texture payload sizes and FNV-1a hashes. Tests skip when the models are absent;
CI or another workstation can
override their locations with `TERMIN_GLB_GEOMETRY_REFERENCE` and
`TERMIN_GLB_TEXTURED_REFERENCE`. Payload hashing is an opt-in diagnostic path
and is not performed by normal `build_mesh()` calls.

On the geometry-only reference, the native importer produces 2,823,922
vertices and 17,280,924 indices in a 12-byte position-only layout. The payload
is 103,010,760 bytes with hash `5462061671071880564`. A measured local run
opened the mapped document in 38.7 ms and built plus hashed it in 142.4 ms;
maximum process RSS was 256,608 KiB. Twelve repeated load/build/destroy cycles
stabilized at 51,948 KiB after warm-up (8 KiB tail growth).

On the textured reference, the PBR-compatible generated-tangent payload is
47,775,828 bytes with aggregate geometry hash `2595364662792861569`. The
selected encoded images total 3,604,320 bytes; their image, material, and
texture hashes remain independently pinned by the reference test.

## Rig migration boundary

The document exposes node, skin, animation sampler, and channel discovery as
one bulk Python crossing. Animation input/output tensors are flattened float32
pools with explicit component count, interpolation, target node index, and
target path; inverse-bind matrices cross the ABI in the project-wide
column-major storage order. This preserves `STEP`, non-uniform vec3 scale, morph-weight cardinality,
and cubic-spline tensor shape instead of lowering them prematurely to the
legacy animation-channel model.

The Arthur reference pins 20 skinned meshes, 118,693 vertices, 573,963
indices, an 11,791,292-byte mesh payload and geometry hash
`14695474819576827200`. Its bulk rig snapshot pins 101 nodes, one 80-joint
skin, 53 animations, 12,702 samplers/channels, and raw tensor hash
`11954369855452468884`. Separate semantic assertions retain all 3,792 STEP
samplers and 683 non-uniform scale values.

Runtime animation publication is not routed through the old
`TcAnimationClip.set_channels()` adapter. `build_animation_clip()` converts a
selected native animation into owned bulk tracks in one binding call, retaining
the target node index, interpolation, vec3 scale, and exact cubic/morph payload.
`AnimationPlayer` resolves these tracks through an exact node-indexed entity
table, so duplicate names cannot retarget an animation. LINEAR and STEP tracks
play directly; CUBICSPLINE and morph weights are preserved but fail loudly at
the sampling boundary until their runtime lowering is implemented.

Animation clip names themselves must currently be unique because
`AnimationPlayer` selects clips by name. The cgltf production bridge rejects a
document with duplicate clip names instead of allowing the player map to
overwrite one clip silently.

The current bridge can optionally apply the shared Y-up to Z-up basis mapping
to translation, rotation, scale, and cubic tangent tuples. Native node/skin
preparation applies the same policy to node TRS and inverse-bind matrices.
`build_skeleton()` derives each bone parent from the nearest joint ancestor and
publishes all bones and roots transactionally; failed validation or allocation
leaves the prior resource/version intact. The optional Blender compatibility
policy is explicit and is applied after basis conversion to both affected rest
TRS and animation tuples, while intentionally leaving IBM unchanged to retain
the established importer contract. Callers must still select the same options
for prepared rest data, skeletons, meshes, and clips.

The Arthur editor smoke gate exercises this production route end to end: 20
skinned meshes, one 80-bone skeleton, 53 clips and 12,702 exact tracks are
published, instantiated, and sampled. Repeated reload replaces their payloads
under stable child UUIDs.
