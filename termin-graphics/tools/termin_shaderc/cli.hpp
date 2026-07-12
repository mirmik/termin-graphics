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
    std::string fxc;
    std::string matrix_layout = "column";
    std::string default_scope;
    std::vector<std::string> include_dirs;
    std::vector<std::string> program_sources;
};

struct ParsedCommandLine {
    CompileOptions options;
    bool should_compile = false;
    int exit_code = 0;
};

ParsedCommandLine parse_command_line(int argc, char** argv);

} // namespace termin_shaderc
