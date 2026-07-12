#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include <termin/materials/termin_materials_api.h>

namespace termin {

/**
 * GLSL Preprocessor with #include support.
 *
 * Include files must be registered before preprocessing.
 * Supports recursive includes with cycle detection.
 */
class GlslPreprocessor {
private:
    std::unordered_map<std::string, std::string> includes_;

public:
    /**
     * Register an include file.
     *
     * @param name   Include name (e.g., "shadows", "lighting")
     * @param source GLSL source code
     */
    TERMIN_MATERIALS_API void register_include(const std::string& name, const std::string& source);

    /**
     * Check if an include is registered.
     */
    TERMIN_MATERIALS_API bool has_include(const std::string& name) const;

    /**
     * Get registered include source.
     */
    TERMIN_MATERIALS_API const std::string* get_include(const std::string& name) const;

    /**
     * Clear all registered includes.
     */
    TERMIN_MATERIALS_API void clear();

    /**
     * Get number of registered includes.
     */
    TERMIN_MATERIALS_API size_t size() const;

    /**
     * Check if source contains #include directives.
     */
    TERMIN_MATERIALS_API static bool has_includes(const std::string& source);

    /**
     * Preprocess GLSL source, resolving #include directives.
     *
     * @param source      GLSL source code
     * @param source_name Name for error messages
     * @return Processed source with includes resolved
     * @throws std::runtime_error if include not found or circular include
     */
    TERMIN_MATERIALS_API std::string preprocess(const std::string& source, const std::string& source_name = "<unknown>");

private:
    std::string preprocess_impl(
        const std::string& source,
        const std::string& source_name,
        std::unordered_set<std::string>& included
    );

};

/**
 * Global GLSL preprocessor instance.
 * Defined in glsl_preprocessor.cpp to ensure single instance across DLLs.
 */
TERMIN_MATERIALS_API GlslPreprocessor& glsl_preprocessor();

} // namespace termin
