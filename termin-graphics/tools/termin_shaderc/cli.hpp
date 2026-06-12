#pragma once

#include <string>
#include <vector>

namespace termin_shaderc {

struct CompileOptions {
    std::string language = "glsl";
    std::string target;
    std::string stage;
    std::string input;
    std::string output;
    std::string entry = "main";
    std::string debug_name = "shader";
    std::string slangc;
    std::string matrix_layout = "column";
    std::string layout_scheme = "per-pipeline";  // shader-driven, Godot 4 model
    std::vector<std::string> include_dirs;
};

struct ParsedCommandLine {
    CompileOptions options;
    bool should_compile = false;
    int exit_code = 0;
};

ParsedCommandLine parse_command_line(int argc, char** argv);

} // namespace termin_shaderc
