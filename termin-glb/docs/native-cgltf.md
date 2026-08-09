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

