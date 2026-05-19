#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <functional>

namespace termin {

/**
 * GLSL Preprocessor with #include support.
 *
 * Include files must be registered before preprocessing.
 * Supports recursive includes with cycle detection.
 * Has a fallback loader callback for lazy-loading from Python.
 */
class GlslPreprocessor {
public:
    // Callback type for fallback loading: takes include name, returns true if loaded
    using FallbackLoader = std::function<bool(const std::string&)>;

    /**
     * Set fallback loader callback.
     * Called when an include is not found in the registry.
     * The callback should load the include and register it, then return true.
     */
    void set_fallback_loader(FallbackLoader loader) {
        fallback_loader_ = std::move(loader);
    }

    /**
     * Register an include file.
     *
     * @param name   Include name (e.g., "shadows", "lighting")
     * @param source GLSL source code
     */
    void register_include(const std::string& name, const std::string& source) {
        includes_[name] = source;
    }

    /**
     * Check if an include is registered.
     */
    bool has_include(const std::string& name) const {
        return includes_.find(name) != includes_.end();
    }

    /**
     * Get registered include source.
     */
    const std::string* get_include(const std::string& name) const {
        auto it = includes_.find(name);
        return it != includes_.end() ? &it->second : nullptr;
    }

    /**
     * Clear all registered includes.
     */
    void clear() {
        includes_.clear();
    }

    /**
     * Get number of registered includes.
     */
    size_t size() const {
        return includes_.size();
    }

    /**
     * Check if source contains #include directives.
     */
    static bool has_includes(const std::string& source) {
        // Simple check: look for "#include" anywhere in the source
        // This is faster than regex and sufficient for our use case
        return source.find("#include") != std::string::npos;
    }

    /**
     * Preprocess GLSL source, resolving #include directives.
     *
     * @param source      GLSL source code
     * @param source_name Name for error messages
     * @return Processed source with includes resolved
     * @throws std::runtime_error if include not found or circular include
     */
    std::string preprocess(const std::string& source, const std::string& source_name = "<unknown>") {
        std::unordered_set<std::string> included;
        return preprocess_impl(source, source_name, included);
    }

private:
    std::string preprocess_impl(
        const std::string& source,
        const std::string& source_name,
        std::unordered_set<std::string>& included
    ) {
        static const std::regex include_pattern(R"(^\s*#\s*include\s+[<"]([^>"]+)[>"])");

        std::string result;
        result.reserve(source.size());

        std::istringstream stream(source);
        std::string line;

        while (std::getline(stream, line)) {
            std::smatch match;
            if (std::regex_search(line, match, include_pattern)) {
                std::string include_name = match[1].str();

                // Check for circular include
                if (included.find(include_name) != included.end()) {
                    throw std::runtime_error(
                        "Circular include detected: '" + include_name +
                        "' (included from '" + source_name + "')"
                    );
                }

                // Get include source
                auto it = includes_.find(include_name);
                if (it == includes_.end()) {
                    // Try fallback loader
                    if (fallback_loader_ && fallback_loader_(include_name)) {
                        it = includes_.find(include_name);
                    }
                    if (it == includes_.end()) {
                        throw std::runtime_error(
                            "GLSL include not found: '" + include_name +
                            "' (included from '" + source_name + "')"
                        );
                    }
                }

                // Recursively preprocess
                std::unordered_set<std::string> new_included = included;
                new_included.insert(include_name);

                std::string processed = preprocess_impl(it->second, include_name, new_included);

                // Add markers for debugging
                result += "// === BEGIN INCLUDE: " + include_name + " ===\n";
                result += processed;
                result += "\n// === END INCLUDE: " + include_name + " ===\n";
            } else {
                result += line;
                result += '\n';
            }
        }

        return result;
    }

    std::unordered_map<std::string, std::string> includes_;
    FallbackLoader fallback_loader_;
};

/**
 * Global GLSL preprocessor instance.
 * Defined in glsl_preprocessor.cpp to ensure single instance across DLLs.
 */
GlslPreprocessor& glsl_preprocessor();

} // namespace termin
