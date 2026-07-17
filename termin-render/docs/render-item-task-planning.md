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

`RenderItemEncoderCapabilities` is immutable, coarse metadata. Its pass-output,
vertex-transform, and task-input masks allow tooling and passes to discover an
encoder's broad shape, but a matching bit is not an acceptance decision.

`RenderItemTaskPlanningContract` is the authoritative per-pass request. It
defines:

- the requested pass output ABI (`RenderItemPassSemantic`);
- whether a material phase is required, optional, or forbidden;
- pass-provided and pass-required task inputs, including draw context and
  override color;
- accepted vertex-transform kinds;
- the borrowed `MaterialPipelinePassContract` used by shader planning.

`plan_render_item_task` validates the complete request, invokes the registered
shader planner, and appends a `RenderTask` only after every check succeeds. A
rejected request leaves the task list unchanged, returns a stable
`RenderItemTaskRejection` code, and emits a diagnostic containing the pass,
item kind, encoder, reason, and detail.

The result identifies an accepted task by list index rather than a pointer;
task-vector growth therefore cannot invalidate the planning result.

## Shader planning migration boundary

Every encoder registration provides `plan_task_shader` explicitly. The current
passthrough hook preserves a candidate shader for item kinds that have not yet
migrated shader selection. Mesh already uses an item-aware hook to select its
static or skinned transform kind, and foliage selects its color or shadow
transform kind.

The hook is the migration boundary for removing Drawable shader callbacks: an
encoder can select or assemble the final shader from the typed item and the
pass-owned material-pipeline contract without adding another Drawable ABI.
Backend draw behavior remains exclusively in the encoder's `encode` callback.

Passes that build `RenderTask` records must not replace planning with item-kind
switches or a `pass_semantic_mask` check. The older Geometry/Depth draw-call
paths still use discovery checks until their #205 task/shader-planning
migration. Unsupported combinations in the shared task path belong in the
structured planning result; new compatibility dimensions belong in the pass
contract or an item-kind planner, not in per-pass boolean matrices.
