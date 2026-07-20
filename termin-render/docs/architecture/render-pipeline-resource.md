# Render pipeline resource and execution instances

`tc_render_pipeline` is the canonical, backend-independent identity of a
compiled render pipeline. It is a versioned `tc_resource_header` resource in a
process-global UUID registry. Its payload contains only the ordered pass plan,
opaque pass parameter payloads, resource specifications, pass/resource
dependencies, and viewport/export target metadata.

The canonical resource must never contain an `IRenderDevice`, GPU handle,
frame graph, live `tc_pass`, scene handle, FBO pool, texture pool, shadow map,
or another mutable execution cache. Graph compiler input is not part of this
contract: `graph_compiler` lowers graph data into the descriptor, after which
the resource can be serialized and consumed without the graph/editor layer.

## Execution state

`tc_pipeline` is the execution-instance ABI. The C++ spelling is
`RenderPipelineInstance`; `RenderPipeline` remains its source-compatible name
during migration. Each instance owns its live passes, cached frame graph and
device-local `PipelineRenderCache`, and holds one strong reference to its
canonical `tc_render_pipeline`.

Create additional instances with `tc_pipeline_create_from_resource()` or the
`RenderPipeline(const TcRenderPipeline&)` constructor. Instances sharing a
resource never share live passes, frame graphs, dirty state, FBOs, textures, or
other device caches. Destroying an instance releases only its own execution
state and its strong resource reference.

Raw C handles are weak, generation-checked identities. Owners retain the
resource with `tc_render_pipeline_retain()` and release it with
`tc_render_pipeline_release()`. The final release removes the UUID mapping and
invalidates the pool generation. `TcRenderPipeline` provides this ownership
contract as C++ RAII.

Pipeline assets declare this resource with the UUID stored in the asset
document and compile their graph directly into it. The execution instance's
`canonical_resource.uuid` is therefore the persisted asset identity; callers
must not reconstruct canonical identity by looking an instance up by name.

## Descriptor format

`tc_render_pipeline_payload_desc.descriptor_version` is currently `1`.
`tc_render_pipeline_set_payload()` validates and atomically replaces the full
payload, then increments the resource version. Partial mutation is not part of
the compiled-resource contract.

`tc_render_pipeline_serialize()` writes the versioned `TRPL` binary envelope.
The envelope records its own binary version and the descriptor version, then
all ordered descriptor arrays. Call it first with a null output to obtain the
required byte count. `tc_render_pipeline_deserialize()` rejects unknown
versions, malformed counts, truncated input and trailing bytes; it publishes
the UUID only after the full descriptor can be reconstructed.
Integer and IEEE-754 single-precision fields use little-endian byte order;
strings use a little-endian 32-bit byte length followed by UTF-8 bytes.

Pass `parameters` are opaque serialized values at this layer. Their schema is
owned by the pass type named in the same pass descriptor. The canonical C
resource layer therefore does not depend on graph JSON, editor types, build
artifacts, or a graphics backend.
