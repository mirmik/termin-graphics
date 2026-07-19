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

Parsing `.shader` assets and migrating `TcMaterial` consumers are separate
layers. They populate or consume this resource but do not change its lifecycle.
