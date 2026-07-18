# RenderItem task planning contract

`RenderItem` discovery, pass planning, and backend submission have separate
ownership boundaries:

- an item producer publishes a typed `tc_render_item`;
- the package that owns the item kind registers its encoder, coarse discovery
  capabilities, and shader-planning hook;
- a render pass owns `RenderItemTaskPlanningContract` and asks
  `plan_render_item_task` to append an owned `RenderTask`;
- `submit_render_item_draw` executes an accepted task and does not decide pass
  membership.

## Discovery versus planning

`RenderItemEncoderCapabilities` is immutable, coarse metadata. Its phase,
vertex-transform, and task-input masks allow tooling and passes to discover an
encoder's broad shape, but a matching bit is not an acceptance decision.

`RenderItemTaskPlanningContract` is the authoritative per-pass request. It
defines:

- the requested single-bit render phase;
- whether a material phase is required, optional, or forbidden;
- pass-provided and pass-required task inputs, including draw context and
  override color;
- accepted vertex-transform kinds;
- the borrowed `MaterialPipelinePassContract` used by shader planning.

`plan_render_item_task` validates the complete request, invokes the registered
shader planner, and appends a `RenderTask` only after every check succeeds. The
task owns both the final draw shader and the deduplicated set of shader usages
required to package or precompile that draw. A rejected request leaves the task
list unchanged, returns a stable `RenderItemTaskRejection` code, and emits a
diagnostic containing the pass, item kind, encoder, reason, and detail.

The result identifies an accepted task by list index rather than a pointer;
task-vector growth therefore cannot invalidate the planning result.

## Shader planning migration boundary

Every encoder registration provides `plan_task_shader` explicitly. The
passthrough hook is reserved for item kinds whose draw shader is exactly the
pass candidate. Mesh selects static or skinned transforms, foliage assembles
its instanced variant, and line batches select their mode-specific variant and
enumerate auxiliary shaders such as tube caps.

The planner is the sole shader-selection boundary. It selects or assembles the
final shader and enumerates all shader usages from the typed item and the
pass-owned material-pipeline contract. `Drawable` only reports phase
participation and collects typed items; its C ABI has no shader override or
shader-usage callbacks. Backend draw behavior remains exclusively in the
encoder's `encode` callback.

Passes that build `RenderTask` records must not replace planning with item-kind
switches or a separate pass-semantic check. Color, shadow, geometry, normal, ID,
and depth-only paths all use the shared planner. Unsupported combinations
belong in the structured planning result; new compatibility dimensions belong
in the pass contract or an item-kind planner, not in per-pass boolean matrices.
