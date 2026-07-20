# Canonical Shader Program Resource

`tc_shader_program` is the canonical, backend-independent identity of an
authored multi-phase shader program. Its UUID registry and payload live in
`termin-graphics`; they do not contain a render device, compiled backend
modules, pipelines, or device caches.

The payload contains program metadata, the ordered material-property schema,
and ordered phase descriptors. Each phase owns a strong `tc_shader_handle`.
The shader UUID is derived deterministically from the program UUID and phase
mark with `tc_shader_program_make_phase_uuid()`, so reloads preserve phase
identity even when the payload is rebuilt.

`tc_shader_program_set_payload()` builds and validates a complete replacement
before publishing it. A rejected property or phase leaves the previous
payload and version unchanged. A successful replacement swaps the arrays,
releases old phase handles, and increments the program version once.

Ownership follows the canonical resource contract:

- `create` rejects duplicate UUIDs; `declare`/`get_or_create` are idempotent;
- `find` returns a non-retained C handle;
- C consumers use `retain`/`release`; `TcShaderProgram` and the Python binding
  retain automatically;
- the last retained handle removes the UUID mapping and releases all phase
  shaders;
- explicit `remove` is accepted only when no retained handle exists.

The authored `.shader` importer parses `ShaderMultyPhaseProgramm` only as a
temporary source-domain IR. `ShaderAsset` declares and strongly retains the
program handle with the catalog UUID, atomically replaces the program payload,
and publishes each parsed phase into the `TcShader` owned by the corresponding
phase descriptor. Neither the parsed IR nor a parallel name-keyed program cache
survives the import.

`TcMaterial` resolves its shader reference to a canonical program UUID. Its
phases, render state, defaults, property schema and phase shader handles are
read from `TcShaderProgram`; the material records the program UUID and version
used to build it. Source-only reloads keep phase identity and merely advance
that dependency version, while schema changes rebuild loaded materials and
invalidate dependent material-pass pipelines.

Runtime packages serialize this layer explicitly as versioned
`shaders/*.shader-program.json` resources. A program descriptor contains the
backend-independent property schema, ordered phase metadata, render states and
references to the independently packaged phase `TcShader` resources. Backend
artifacts remain attached to those phase shader resources rather than becoming
program payload. `termin-runtime` loads phase shaders first, reconstructs each
`TcShaderProgram` without the authored `.shader` parser, then restores material
program UUID/version dependencies. An unsupported descriptor schema, a missing
phase shader or a phase UUID that does not match the deterministic program
identity fails package loading with a diagnostic.
