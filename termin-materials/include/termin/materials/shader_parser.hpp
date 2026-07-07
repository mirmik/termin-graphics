#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>
#include <stdexcept>
#include <algorithm>

#include <termin/materials/termin_materials_api.h>

namespace termin {

/**
 * Material property for inspector.
 *
 * Types: Float, Int, Bool, Vec2, Vec3, Vec4, Color, Texture
 */
struct MaterialProperty {
    std::string name;
    std::string property_type;  // "Float", "Int", "Bool", "Vec2", etc.

    // Default value as variant
    using DefaultValue = std::variant<
        std::monostate,         // None/Texture
        bool,                   // Bool
        int,                    // Int
        double,                 // Float
        std::vector<double>,    // Vec2, Vec3, Vec4, Color
        std::string             // Texture path
    >;
    DefaultValue default_value;

    std::optional<double> range_min;
    std::optional<double> range_max;
    std::optional<std::string> label;

    MaterialProperty() = default;
    MaterialProperty(
        std::string name_,
        std::string type_,
        DefaultValue default_ = std::monostate{},
        std::optional<double> min_ = std::nullopt,
        std::optional<double> max_ = std::nullopt,
        std::optional<std::string> label_ = std::nullopt
    ) : name(std::move(name_)),
        property_type(std::move(type_)),
        default_value(std::move(default_)),
        range_min(min_),
        range_max(max_),
        label(std::move(label_)) {}
};

// Alias for backward compatibility
using UniformProperty = MaterialProperty;


/**
 * Single shader stage (vertex, fragment, geometry).
 */
struct ShaderStage {
    std::string name;
    std::string source;
    std::string entry = "main";

    ShaderStage() = default;
    ShaderStage(std::string name_, std::string source_)
        : name(std::move(name_)), source(std::move(source_)) {}
    ShaderStage(std::string name_, std::string source_, std::string entry_)
        : name(std::move(name_)),
          source(std::move(source_)),
          entry(entry_.empty() ? "main" : std::move(entry_)) {}
};


/**
 * One field inside the generated std140 material UBO block.
 * name matches the original @property; property_type is the same string
 * used in MaterialProperty ("Float", "Vec3", ...); offset and size are
 * computed per std140 rules and describe the layout inside the block.
 */
struct MaterialUboEntry {
    std::string name;
    std::string property_type;
    uint32_t offset = 0;
    uint32_t size = 0;
};


/**
 * std140 layout description for the per-phase material UBO.
 *
 * Populated at parse time when the shader program has the `material_ubo`
 * feature. `entries` holds only the scalar/vector properties (not textures),
 * in declaration order, with std140 offsets. `block_size` is the total UBO
 * size rounded up to 16 bytes as required by std140.
 */
struct MaterialUboLayout {
    std::vector<MaterialUboEntry> entries;
    uint32_t block_size = 0;

    bool empty() const { return entries.empty(); }
};


/**
 * Render state settings for a specific phase mark.
 */
struct PhaseRenderSettings {
    std::optional<bool> gl_depth_mask;
    std::optional<bool> gl_depth_test;
    std::optional<bool> gl_blend;
    std::optional<bool> gl_cull;
    int priority = 0;
};

/**
 * Shader phase: stages + render state flags + compiled material interface.
 */
struct ShaderPhase {
    std::string phase_mark;  // Primary/default mark
    std::vector<std::string> available_marks;  // All available marks (for user choice)
    int priority = 0;

    // Render state flags (null = not specified, use default)
    std::optional<bool> gl_depth_mask;
    std::optional<bool> gl_depth_test;
    std::optional<bool> gl_blend;
    std::optional<bool> gl_cull;

    // Per-mark render settings (from @settings blocks)
    std::unordered_map<std::string, PhaseRenderSettings> mark_settings;

    // Stages by name (vertex, fragment, geometry)
    std::unordered_map<std::string, ShaderStage> stages;

    // Material properties used by this phase after preprocessing. The
    // canonical inspector schema lives on ShaderMultyPhaseProgramm.
    std::vector<MaterialProperty> uniforms;

    // Plain GLSL material-block uniforms that are controlled by
    // passes/controllers, not by the material inspector.
    std::vector<MaterialProperty> material_uniforms;

    // std140 layout for the auto-generated material UBO. Empty unless the
    // parser sees scalar/vector @property or plain uniform entries; in that
    // case it rewrites the stage sources to reference the generated block.
    MaterialUboLayout material_ubo_layout;

    // Parser-synthesized GLSL resources that must be mirrored into
    // tc_shader_resource_binding for bind-by-name runtime code.
    std::vector<std::string> material_texture_resources;
    bool uses_engine_per_frame = false;
    bool uses_engine_draw_data = false;

    ShaderPhase() = default;
    ShaderPhase(std::string mark) : phase_mark(std::move(mark)) {
        available_marks.push_back(phase_mark);
    }
    ShaderPhase(std::vector<std::string> marks)
        : phase_mark(marks.empty() ? "" : marks[0]),
          available_marks(std::move(marks)) {}
};


/**
 * Multi-phase shader program.
 */
class ShaderMultyPhaseProgramm {
public:
    std::string program;  // Program name
    std::string language = "glsl";  // Source language: glsl or slang.
    std::vector<ShaderPhase> phases;
    std::string source_path;
    std::vector<std::string> features;  // Feature flags (e.g., "lighting_ubo")
    std::vector<MaterialProperty> material_properties;  // Canonical material inspector schema.

    ShaderMultyPhaseProgramm() = default;
    ShaderMultyPhaseProgramm(
        std::string program_,
        std::vector<ShaderPhase> phases_,
        std::string source_path_ = "",
        std::vector<std::string> features_ = {},
        std::vector<MaterialProperty> material_properties_ = {}
    ) : program(std::move(program_)),
        phases(std::move(phases_)),
        source_path(std::move(source_path_)),
        features(std::move(features_)),
        material_properties(std::move(material_properties_)) {}

    /**
     * Check if shader has a specific feature.
     */
    bool has_feature(const std::string& feature) const {
        return std::find(features.begin(), features.end(), feature) != features.end();
    }

    /**
     * Get phase by mark.
     */
    const ShaderPhase* get_phase(const std::string& mark) const {
        for (const auto& phase : phases) {
            if (phase.phase_mark == mark) {
                return &phase;
            }
        }
        return nullptr;
    }
};


// ========== Parser Functions ==========

/**
 * Parse shader text in custom format.
 *
 * Supported directives:
 *   @program <name>
 *   @language slang                 // Required. GLSL .shader programs are not supported.
 *
 *   // Traditional multi-phase (explicit):
 *   @phase <mark>
 *   @priority <int>
 *   @glDepthMask <bool>
 *   @glDepthTest <bool>
 *   @glBlend <bool>
 *   @glCull <bool>
 *   @property <Type> <name> [= DefaultValue] [range(min, max)]
 *      Material-level property. Inside @phase is accepted for legacy syntax,
 *      but per-phase properties are not supported.
 *   @stage <stage_name> [entry_name|entry=<entry_name>]
 *   @endstage
 *   @endphase
 *
 *   // Shared stages multi-phase (new syntax):
 *   @phases <mark1>, <mark2>, ...     // Declares phases with shared code
 *   @settings <mark>                  // Per-phase render state overrides
 *   @endsettings                      // Optional end of settings block
 *   @property ...                     // Material-level properties
 *   @stage vertex / @stage fragment   // Shared stages (outside @phase)
 *                                      // Entry defaults to "main".
 */
TERMIN_MATERIALS_API ShaderMultyPhaseProgramm parse_shader_text(const std::string& text);

/**
 * Parse bool from string.
 */
TERMIN_MATERIALS_API bool parse_bool(const std::string& value);

/**
 * Parse @property directive.
 */
TERMIN_MATERIALS_API MaterialProperty parse_property_directive(const std::string& line);

// ========== std140 Material UBO generator ==========

/**
 * Compute std140 (size, alignment) in bytes for a single material property
 * type name. Textures return {0, 0}. Unknown types return {0, 0}.
 *
 * std140 rules:
 *   Float / Int / Bool  : size=4,  align=4
 *   Vec2                : size=8,  align=8
 *   Vec3                : size=12, align=16
 *   Vec4 / Color        : size=16, align=16
 */
TERMIN_MATERIALS_API std::pair<uint32_t, uint32_t> std140_size_align(const std::string& property_type);

/**
 * Compute a MaterialUboLayout for the given ordered list of properties.
 * Texture properties are skipped (they become samplers, not UBO members).
 */
TERMIN_MATERIALS_API MaterialUboLayout compute_std140_layout(const std::vector<MaterialProperty>& properties);

/**
 * Produce the GLSL text for a `layout(std140) uniform MaterialParams { ... };`
 * block matching the given layout. The returned string includes a trailing
 * newline. Empty layout yields an empty string.
 */
TERMIN_MATERIALS_API std::string synthesize_material_ubo_glsl(const MaterialUboLayout& layout);

/**
 * Produce the Slang text for a `MaterialParams` struct plus a backend-neutral
 * `ConstantBuffer<MaterialParams> material` declaration matching the given
 * std140 layout. Backend binding assignment is captured by compiled artifact
 * layout metadata.
 */
TERMIN_MATERIALS_API std::string synthesize_material_ubo_slang(const MaterialUboLayout& layout);

/**
 * Remove top-level `uniform <type> <name>;` declarations whose names are in
 * `names`. Works line-oriented; lines that do not look like a simple uniform
 * declaration are preserved as-is.
 */
TERMIN_MATERIALS_API std::string strip_uniform_decls(
    const std::string& source,
    const std::vector<std::string>& names
);

/**
 * Insert a GLSL text block into a shader source immediately after its
 * `#version ...` line (or at the top if there is none).
 */
TERMIN_MATERIALS_API std::string inject_after_version(const std::string& source, const std::string& block);

/**
 * Apply the same engine uniform rewrite used by .shader assets to a raw
 * GLSL stage source created through TcMaterial.add_phase_from_sources().
 *
 * This strips plain u_model/u_view/u_projection-style declarations and
 * injects parser-owned PerFrame/DrawData resource blocks when the stage
 * references those names. It also normalizes #version to 450 core.
 */
TERMIN_MATERIALS_API std::string rewrite_engine_uniforms_for_stage_source(
    const std::string& source,
    const std::string& stage_name
);

/**
 * Pack material property values into a std140-laid out byte buffer.
 *
 * For each entry in `layout.entries`, looks up a property with matching name
 * in `values` and writes its value at `entry.offset`. Missing properties or
 * type mismatches are silently skipped (the caller is responsible for
 * zero-filling `out_buffer` beforehand if it wants deterministic defaults).
 *
 * The buffer pointed to by `out_buffer` must be at least `layout.block_size`
 * bytes long. Float/vector/matrix values are written as 32-bit floats; Int and
 * Bool values are written as 32-bit integers. `Texture` properties are ignored
 * because they are not in the UBO.
 */
TERMIN_MATERIALS_API void std140_pack(
    const MaterialUboLayout& layout,
    const std::vector<MaterialProperty>& values,
    uint8_t* out_buffer
);

} // namespace termin
