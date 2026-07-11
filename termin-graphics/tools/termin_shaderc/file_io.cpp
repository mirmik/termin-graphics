#include "backend_patchers.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace termin_shaderc::internal {
bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "termin_shaderc: failed to open input: " << path << "\n";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool ensure_parent_directory(const std::filesystem::path& path, const char* label) {
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        std::cerr << "termin_shaderc: failed to create " << label
                  << " directory: " << parent.string() << ": "
                  << ec.message() << "\n";
        return false;
    }
    return true;
}

bool write_spirv(const std::string& path, const std::vector<uint32_t>& words) {
    if (!ensure_parent_directory(path, "output")) {
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "termin_shaderc: failed to open output: " << path << "\n";
        return false;
    }
    out.write(reinterpret_cast<const char*>(words.data()),
              static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
    return static_cast<bool>(out);
}

} // namespace termin_shaderc::internal
