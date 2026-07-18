# Render phase registry

Render phase participation is represented at runtime by `tc_phase_mask`, a
64-bit mask. The same phase bits are used by `Drawable`, scene item routing,
encoder capabilities, and render task planning. There is no separate pass
semantic or output-kind participation mask.

Bits 0 through 15 are reserved for engine phases. The current assignments are
`opaque`, `transparent`, `normal`, `depth`, `id`, `shadow`, `ui`, `editor`,
`editor_debug`, and `editor_debug_transparent`. The remaining engine-reserved
slots stay empty so new built-in phases do not move project bits.

Bits 16 through 63 belong to the project. Their names are stored as an indexed
48-entry `render_phase_names` array in `project_settings/project.json`. Array
slot 0 maps to bit 16, slot 1 to bit 17, and so on. Empty entries reserve unused
bits. Names must be unique, must not duplicate built-ins, and must be configured
before material and pipeline assets are loaded.

Asset formats and editor controls keep canonical string names. Loading resolves
those names through the already configured project registry. An unknown name is
an error; asset loading never allocates a phase implicitly.
