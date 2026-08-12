#include "glsl_cross_compiler.hpp"

#include <spirv_glsl.hpp>

#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace termin_shaderc::internal {

    namespace {

        bool read_spirv(const std::filesystem::path& path, std::vector<uint32_t>& words, std::string& error) {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) {
                error = "failed to open SPIR-V input: " + path.string();
                return false;
            }
            const std::streamsize byte_count = input.tellg();
            if (byte_count <= 0 || (byte_count % static_cast<std::streamsize>(sizeof(uint32_t))) != 0) {
                error = "invalid SPIR-V byte count for '" + path.string() + "': " + std::to_string(byte_count);
                return false;
            }
            input.seekg(0, std::ios::beg);
            words.resize(static_cast<size_t>(byte_count) / sizeof(uint32_t));
            if (!input.read(reinterpret_cast<char*>(words.data()), byte_count)) {
                error = "failed to read SPIR-V input: " + path.string();
                return false;
            }
            return true;
        }

        bool write_glsl(const std::filesystem::path& path, const std::string& source, std::string& error) {
            std::ofstream output(path, std::ios::binary);
            if (!output) {
                error = "failed to open GLSL output: " + path.string();
                return false;
            }
            output.write(source.data(), static_cast<std::streamsize>(source.size()));
            if (!output) {
                error = "failed to write GLSL output: " + path.string();
                return false;
            }
            return true;
        }

    } // namespace

    bool cross_compile_spirv_to_glsl(const std::filesystem::path& spirv_path,
                                     const std::filesystem::path& output_path,
                                     GlslCrossProfile profile,
                                     std::string& error) {
        std::vector<uint32_t> words;
        if (!read_spirv(spirv_path, words, error)) {
            return false;
        }

        try {
            spirv_cross::CompilerGLSL compiler(std::move(words));
            spirv_cross::CompilerGLSL::Options options = compiler.get_common_options();
            options.version = profile == GlslCrossProfile::Desktop330 ? 330 : 300;
            options.es = profile == GlslCrossProfile::Es300;
            options.vulkan_semantics = false;
            options.separate_shader_objects = false;
            options.enable_420pack_extension = false;
            compiler.set_common_options(options);

            // GLSL 3.30 and GLSL ES 3.00 cannot express descriptor-style
            // binding numbers on uniform blocks. Give every block type the
            // symbolic resource name from reflection so the runtime can map it
            // to the backend binding plan with glUniformBlockBinding(). Keep a
            // distinct instance name to avoid a type/instance identifier clash.
            const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
            const auto normalize_varyings = [&compiler](const auto& varyings) {
                for (const spirv_cross::Resource& resource : varyings) {
                    if (compiler.has_decoration(resource.id, spv::DecorationLocation)) {
                        compiler.set_name(
                            resource.id,
                            "termin_varying_" +
                                std::to_string(compiler.get_decoration(resource.id, spv::DecorationLocation)));
                    }
                }
            };
            const spv::ExecutionModel execution_model = compiler.get_execution_model();
            if (execution_model != spv::ExecutionModelVertex) {
                normalize_varyings(resources.stage_inputs);
            }
            if (execution_model != spv::ExecutionModelFragment) {
                normalize_varyings(resources.stage_outputs);
            }
            for (const spirv_cross::Resource& resource : resources.uniform_buffers) {
                const std::string resource_name = compiler.get_name(resource.id);
                if (!resource_name.empty()) {
                    compiler.set_name(resource.base_type_id, resource_name);
                    compiler.set_name(resource.id, resource_name + "_instance");
                }
            }

            compiler.build_combined_image_samplers();
            for (const spirv_cross::CombinedImageSampler& combined : compiler.get_combined_image_samplers()) {
                const std::string image_name = compiler.get_name(combined.image_id);
                if (!image_name.empty()) {
                    compiler.set_name(combined.combined_id, image_name);
                }
            }

            return write_glsl(output_path, compiler.compile(), error);
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }

} // namespace termin_shaderc::internal
