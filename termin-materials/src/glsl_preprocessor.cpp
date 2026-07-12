// glsl_preprocessor.cpp - Global GLSL preprocessor instance
#include <termin/materials/glsl_preprocessor.hpp>

#include <regex>
#include <sstream>
#include <stdexcept>

namespace termin {

void GlslPreprocessor::register_include(const std::string& name, const std::string& source) {
    includes_[name] = source;
}

bool GlslPreprocessor::has_include(const std::string& name) const {
    return includes_.find(name) != includes_.end();
}

const std::string* GlslPreprocessor::get_include(const std::string& name) const {
    auto it = includes_.find(name);
    return it != includes_.end() ? &it->second : nullptr;
}

void GlslPreprocessor::clear() {
    includes_.clear();
}

size_t GlslPreprocessor::size() const {
    return includes_.size();
}

bool GlslPreprocessor::has_includes(const std::string& source) {
    // Simple check: look for "#include" anywhere in the source
    // This is faster than regex and sufficient for our use case
    return source.find("#include") != std::string::npos;
}

std::string GlslPreprocessor::preprocess(
    const std::string& source,
    const std::string& source_name
) {
    std::unordered_set<std::string> included;
    return preprocess_impl(source, source_name, included);
}

std::string GlslPreprocessor::preprocess_impl(
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
                throw std::runtime_error(
                    "GLSL include not found: '" + include_name +
                    "' (included from '" + source_name + "')"
                );
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

// Single global instance (shared across all modules)
GlslPreprocessor& glsl_preprocessor() {
    static GlslPreprocessor instance;
    return instance;
}

} // namespace termin
