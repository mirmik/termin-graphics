# Graphics repository migration provenance

The initial `termin-graphics` history was filtered from the Termin repository
at source revision `ba48050ff`. The filtered baseline is `940280b0`.

The filter retained the Graphics-owned module closure, standalone showcase and
installed-consumer fixture, relevant build metadata, documentation, scripts,
and third-party pins. It deliberately excluded Core sources and Termin-owned
assets, scene/ECS, components, editor/player, project, bootstrap, and GLB asset
adapters.

History was preserved per retained path rather than imported as a source
snapshot. Core is an immutable installed SDK input; the first extraction line
was developed against Core revision
`d860ca231693ed0b1eaa52e0a6121c15c9e0966b`. The first independently published
line pins Core `4f973b0b81f5857944225ccc4ae3fe1106598689`, which adds the generic domain-SDK
composition contract. Later compatible pins are recorded by Graphics CI and
`sdk-inputs.json`.

The original Termin revision remains the rollback and archaeology point. After
consumer cutover, Termin must keep only integration adapters and must not regain
copies of Graphics-owned sources.
