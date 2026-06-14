#include "cli.hpp"

#include <iostream>
#include <utility>

namespace termin_shaderc {

namespace {

void print_help(std::ostream& out) {
    out
        << "termin_shaderc - Termin shader artifact compiler\n"
        << "\n"
        << "Usage:\n"
        << "  termin_shaderc --help\n"
        << "  termin_shaderc help [compile]\n"
        << "  termin_shaderc compile [options]\n"
        << "\n"
        << "Commands:\n"
        << "  compile              Compile one shader stage and write an artifact.\n"
        << "  help [compile]       Show this help text.\n"
        << "\n"
        << "Compile options:\n"
        << "  --language <name>    Source language: glsl or slang. Default: glsl.\n"
        << "  --target <name>      Backend target: vulkan, opengl, or d3d11.\n"
        << "  --stage <name>       Shader stage: vertex, fragment, or geometry.\n"
        << "  --input <path>       Source file to compile.\n"
        << "  --output <path>      Artifact output path (.spv for Vulkan Slang/GLSL).\n"
        << "  --entry <name>       Entry point. Default: main.\n"
        << "  --debug-name <name>  Name used in diagnostics and artifact metadata.\n"
        << "  -I <dir>             Add an include directory. Repeatable.\n"
        << "  --include-dir <dir>  Same as -I.\n"
        << "\n"
        << "Slang options:\n"
        << "  --slangc <path>          Explicit slangc executable path.\n"
        << "  --matrix-layout <mode>   Matrix layout: column, col, column-major,\n"
        << "                           col-major, row, or row-major. Default: column.\n"
        << "  --layout-scheme <mode>   Descriptor policy: per-pipeline, legacy-engine,\n"
        << "                           or shared. Default: per-pipeline.\n"
        << "\n"
        << "Outputs:\n"
        << "  <output>                 Compiled backend artifact.\n"
        << "  <output>.layout.json     Reflected resource layout sidecar.\n"
        << "  <output>.meta            Written by runtime dev-compile callers, not by\n"
        << "                           standalone termin_shaderc compile.\n"
        << "\n"
        << "Resource scopes:\n"
        << "  Slang resources may use [[TerminScope(\"frame|pass|material|draw|transient\")]].\n"
        << "  If omitted, termin_shaderc infers a scope from resource kind/name and\n"
        << "  writes it to the layout sidecar. Unknown scopes are reported as warnings.\n"
        << "\n"
        << "Examples:\n"
        << "  termin_shaderc compile --language slang --target vulkan --stage vertex \\\n"
        << "    --entry vs_main --input shader.slang --output shader.vert.spv -I builtin_shaders\n"
        << "  termin_shaderc compile --language glsl --target vulkan --stage fragment \\\n"
        << "    --input shader.frag.glsl --output shader.frag.spv\n";
}

void usage() {
    std::cerr
        << "Usage: termin_shaderc compile --language glsl|slang "
        << "--target opengl|vulkan|d3d11 --stage vertex|fragment|geometry "
        << "--input <source> --output <artifact> [--entry main]\n"
        << "Run 'termin_shaderc --help' for full help.\n";
}

} // namespace

ParsedCommandLine parse_command_line(int argc, char** argv) {
    ParsedCommandLine parsed;
    if (argc >= 2) {
        const std::string command = argv[1];
        if (command == "--help" || command == "-h" || command == "help") {
            if (argc > 3 || (argc == 3 && std::string(argv[2]) != "compile")) {
                std::cerr << "termin_shaderc: unknown help topic";
                if (argc >= 3) {
                    std::cerr << ": " << argv[2];
                }
                std::cerr << "\n";
                usage();
                parsed.exit_code = 2;
                return parsed;
            }
            print_help(std::cout);
            parsed.exit_code = 0;
            return parsed;
        }
    }

    if (argc < 2 || std::string(argv[1]) != "compile") {
        usage();
        parsed.exit_code = 2;
        return parsed;
    }

    if (argc == 3) {
        const std::string arg = argv[2];
        if (arg == "--help" || arg == "-h") {
            print_help(std::cout);
            parsed.exit_code = 0;
            return parsed;
        }
    }

    CompileOptions options;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto take_value = [&](std::string& dst) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "termin_shaderc: missing value for " << arg << "\n";
                return false;
            }
            dst = argv[++i];
            return true;
        };

        if (arg == "--target") {
            if (!take_value(options.target)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "--language") {
            if (!take_value(options.language)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "--stage") {
            if (!take_value(options.stage)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "--input") {
            if (!take_value(options.input)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "--output") {
            if (!take_value(options.output)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "--entry") {
            if (!take_value(options.entry)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "--debug-name") {
            if (!take_value(options.debug_name)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "--slangc") {
            if (!take_value(options.slangc)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "--matrix-layout") {
            if (!take_value(options.matrix_layout)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "--layout-scheme") {
            if (!take_value(options.layout_scheme)) {
                parsed.exit_code = 2;
                return parsed;
            }
        } else if (arg == "-I" || arg == "--include-dir") {
            std::string include_dir;
            if (!take_value(include_dir)) {
                parsed.exit_code = 2;
                return parsed;
            }
            options.include_dirs.push_back(std::move(include_dir));
        } else {
            std::cerr << "termin_shaderc: unknown argument: " << arg << "\n";
            usage();
            parsed.exit_code = 2;
            return parsed;
        }
    }

    if (options.target.empty() || options.stage.empty()
        || options.input.empty() || options.output.empty()) {
        usage();
        parsed.exit_code = 2;
        return parsed;
    }

    parsed.options = std::move(options);
    parsed.should_compile = true;
    return parsed;
}

} // namespace termin_shaderc
