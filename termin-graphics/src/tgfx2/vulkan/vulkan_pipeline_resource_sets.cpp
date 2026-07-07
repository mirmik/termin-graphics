#ifdef TGFX2_HAS_VULKAN

#include <vulkan/vulkan.h>

#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "tgfx2/vulkan/vulkan_shader_compiler.hpp"
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"
#include "tgfx2/internal/shader_preprocess.hpp"
#include "tgfx2/internal/shader_logging.hpp"
#include "vulkan_spirv_reflection.hpp"
#include "vulkan_stats.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

extern "C" {
#include <tcbase/tc_log.h>
}

namespace tgfx {

// --- Per-pipeline descriptor set layout ---

static uint64_t hash_descriptor_bindings(
    const std::vector<VkShaderResource::DescriptorBinding>& bindings)
{
    // FNV-1a over (binding, descriptor_type, count). Sorted for determinism
    // because reflection order is not guaranteed stable.
    auto sorted = bindings;
    std::sort(sorted.begin(), sorted.end(),
        [](const VkShaderResource::DescriptorBinding& a,
           const VkShaderResource::DescriptorBinding& b) {
            return a.binding < b.binding;
        });
    uint64_t h = 0xcbf29ce484222325ull;
    for (const auto& b : sorted) {
        h ^= static_cast<uint64_t>(b.binding);
        h *= 0x100000001b3ull;
        h ^= static_cast<uint64_t>(b.descriptor_type);
        h *= 0x100000001b3ull;
        h ^= static_cast<uint64_t>(b.count);
        h *= 0x100000001b3ull;
    }
    return h;
}

VkDescriptorSetLayout VulkanRenderDevice::get_or_create_descriptor_set_layout(
    const std::vector<VkShaderResource::DescriptorBinding>& bindings)
{
    if (bindings.empty()) return VK_NULL_HANDLE;

    const uint64_t hash = hash_descriptor_bindings(bindings);
    {
        auto it = descriptor_layout_cache_.find(hash);
        if (it != descriptor_layout_cache_.end()) return it->second;
    }

    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    // Deduplicate: VS and FS may share the same binding (e.g. per_frame UBO).
    // Keep the first occurrence; count sums if the same binding is used for
    // an array in both stages, but for simplicity we just deduplicate by key.
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> seen;
    for (const auto& b : bindings) {
        auto it = seen.find(b.binding);
        if (it != seen.end()) {
            // Same binding from both stages — keep the one with the higher
            // descriptorCount and merge stageFlags.
            it->second.descriptorCount = std::max(it->second.descriptorCount, b.count);
            it->second.stageFlags |= VK_SHADER_STAGE_ALL_GRAPHICS;
            continue;
        }
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = b.binding;
        lb.descriptorType = b.descriptor_type;
        lb.descriptorCount = b.count;
        lb.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
        seen[b.binding] = lb;
    }
    vk_bindings.reserve(seen.size());
    for (auto& [_, lb] : seen) vk_bindings.push_back(lb);

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(vk_bindings.size());
    ci.pBindings = vk_bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkResult rc = vkCreateDescriptorSetLayout(device_, &ci, nullptr, &layout);
    if (rc != VK_SUCCESS) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice: vkCreateDescriptorSetLayout failed for %zu bindings: VkResult=%d",
               vk_bindings.size(), static_cast<int>(rc));
        descriptor_layout_cache_.emplace(hash, VK_NULL_HANDLE);
        return VK_NULL_HANDLE;
    }
    descriptor_layout_cache_.emplace(hash, layout);
    descriptor_layout_bindings_.emplace(layout, vk_bindings);
    return layout;
}

VkPipelineLayout VulkanRenderDevice::get_or_create_pipeline_layout(
    VkDescriptorSetLayout set_layout)
{
    // A pipeline with no descriptor resources still needs a valid
    // VkPipelineLayout (e.g. for push constants). Vulkan allows
    // setLayoutCount=0; use a sentinel to cache the "null set" layout.
    VkDescriptorSetLayout cache_key = set_layout ? set_layout
        : reinterpret_cast<VkDescriptorSetLayout>(static_cast<uintptr_t>(1));
    {
        auto it = pipeline_layout_cache_.find(cache_key);
        if (it != pipeline_layout_cache_.end()) return it->second;
    }

    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
    pc_range.offset = 0;
    pc_range.size = 128;

    VkPipelineLayoutCreateInfo pl_ci{};
    pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (set_layout) {
        pl_ci.setLayoutCount = 1;
        pl_ci.pSetLayouts = &set_layout;
    }
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &pc_range;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device_, &pl_ci, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }
    pipeline_layout_cache_.emplace(cache_key, layout);
    return layout;
}

// --- Shader ---

ShaderHandle VulkanRenderDevice::create_shader(const ShaderDesc& desc) {
    std::vector<uint32_t> spirv;

    try {
        if (!desc.bytecode.empty()) {
            // Use provided SPIR-V
            spirv.resize(desc.bytecode.size() / 4);
            std::memcpy(spirv.data(), desc.bytecode.data(), desc.bytecode.size());
        } else if (!desc.source.empty()) {
            // Resolve #include / @features etc. through the shared
            // preprocessor hook, then compile the GLSL to SPIR-V via
            // shaderc. OpenGL runs the same preprocess step — shaders
            // stay identical across backends.
            auto t_pre0 = std::chrono::steady_clock::now();
            std::string resolved = internal::preprocess_shader_source(desc.source);
            auto t_pre1 = std::chrono::steady_clock::now();
            g_shader_preprocess_us.fetch_add(
                std::chrono::duration_cast<std::chrono::microseconds>(t_pre1 - t_pre0).count(),
                std::memory_order_relaxed);

            auto t_compile0 = std::chrono::steady_clock::now();
            auto result = vk::compile_glsl_to_spirv(resolved, desc.stage, desc.entry_point);
            auto t_compile1 = std::chrono::steady_clock::now();
            g_shader_compile_us.fetch_add(
                std::chrono::duration_cast<std::chrono::microseconds>(t_compile1 - t_compile0).count(),
                std::memory_order_relaxed);
            if (!result.success) {
                throw std::runtime_error("Shader compilation failed: " + result.error_message);
            }
            spirv = std::move(result.spirv);
        } else {
            return {0};
        }

        VkShaderResource res;
        res.stage = desc.stage;
        res.entry_point = desc.entry_point;
        res.debug_name = desc.debug_name;
        if (!spirv.empty()) {
            std::string reflected_entry =
                reflect_spirv_stage_entry_point(spirv, desc.stage);
            if (!reflected_entry.empty()) {
                if (reflected_entry != desc.entry_point &&
                    internal::shader_verbose_logging_enabled()) {
                    tc_log(TC_LOG_DEBUG,
                        "[VulkanRenderDevice] shader entry remapped from source entry "
                        "to SPIR-V entry: debug='%s' stage=%d source_entry='%s' "
                        "spirv_entry='%s'",
                        desc.debug_name.c_str(),
                        static_cast<int>(desc.stage),
                        desc.entry_point.c_str(),
                        reflected_entry.c_str());
                }
                res.entry_point = std::move(reflected_entry);
            }
        }
        if (desc.stage == ShaderStage::Vertex) {
            auto t_reflect0 = std::chrono::steady_clock::now();
            SpirvVertexInputs inputs = reflect_spirv_vertex_inputs(spirv, res.entry_point);
            auto t_reflect1 = std::chrono::steady_clock::now();
            g_shader_reflect_us.fetch_add(
                std::chrono::duration_cast<std::chrono::microseconds>(t_reflect1 - t_reflect0).count(),
                std::memory_order_relaxed);
            res.vertex_input_locations_known = inputs.known;
            res.vertex_input_locations = std::move(inputs.locations);
        }

        // Reflect descriptor bindings from SPIR-V for per-pipeline layouts.
        res.descriptor_bindings = reflect_spirv_descriptor_bindings(spirv);

        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spirv.size() * sizeof(uint32_t);
        ci.pCode = spirv.data();

        auto t_module0 = std::chrono::steady_clock::now();
        if (vkCreateShaderModule(device_, &ci, nullptr, &res.module) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module");
        }
        auto t_module1 = std::chrono::steady_clock::now();
        g_shader_module_us.fetch_add(
            std::chrono::duration_cast<std::chrono::microseconds>(t_module1 - t_module0).count(),
            std::memory_order_relaxed);
        g_shader_count.fetch_add(1, std::memory_order_relaxed);

        return {shaders_.add(std::move(res))};
    } catch (const std::bad_alloc& e) {
        tc_log(TC_LOG_ERROR,
            "[VulkanRenderDevice] create_shader bad_alloc: debug='%s' stage=%d entry='%s' "
            "source_bytes=%zu bytecode_bytes=%zu spirv_words=%zu: %s",
            desc.debug_name.c_str(),
            static_cast<int>(desc.stage),
            desc.entry_point.c_str(),
            desc.source.size(),
            desc.bytecode.size(),
            spirv.size(),
            e.what());
        throw;
    } catch (const std::exception& e) {
        tc_log(TC_LOG_ERROR,
            "[VulkanRenderDevice] create_shader failed: debug='%s' stage=%d entry='%s' "
            "source_bytes=%zu bytecode_bytes=%zu spirv_words=%zu: %s",
            desc.debug_name.c_str(),
            static_cast<int>(desc.stage),
            desc.entry_point.c_str(),
            desc.source.size(),
            desc.bytecode.size(),
            spirv.size(),
            e.what());
        throw;
    }
}

// --- Pipeline ---

PipelineHandle VulkanRenderDevice::create_pipeline(const PipelineDesc& desc) {
    VkPipelineResource res;
    res.desc = desc;

    // Build per-pipeline VkDescriptorSetLayout from VS+FS SPIR-V reflection.
    // Merged bindings from both stages; duplicates resolved by binding number.
    std::vector<VkShaderResource::DescriptorBinding> merged_bindings;
    auto* vs = get_shader(desc.vertex_shader);
    auto* fs = get_shader(desc.fragment_shader);
    if (vs) {
        merged_bindings.insert(merged_bindings.end(),
            vs->descriptor_bindings.begin(), vs->descriptor_bindings.end());
    }
    if (fs) {
        merged_bindings.insert(merged_bindings.end(),
            fs->descriptor_bindings.begin(), fs->descriptor_bindings.end());
    }
    res.descriptor_set_layout =
        get_or_create_descriptor_set_layout(merged_bindings);
    res.layout = get_or_create_pipeline_layout(res.descriptor_set_layout);

    if (merged_bindings.empty()) {
        tc_log(TC_LOG_WARN,
               "VulkanRenderDevice: create_pipeline vs='%s' fs='%s' — "
               "no descriptor bindings reflected from SPIR-V",
               vs ? vs->debug_name.c_str() : "null",
               fs ? fs->debug_name.c_str() : "null");
    }

    // Get or create render pass from formats. `needs_depth` is driven
    // by the attachment presence (depth_format != Undefined), NOT by the
    // depth_test/depth_write flags — a pipeline that has depth_test off
    // still needs a render pass compatible with passes that carry a
    // depth attachment. Conversely a pass with no depth attachment must
    // produce a pipeline whose cached RP also has no depth slot, or
    // vkCmdDraw fails with "RenderPasses incompatible".
    // Pass the raw color_formats through — depth-only passes (ShadowPass,
    // DepthPass) carry an empty list, and the pipeline's cached render
    // pass must also have zero color attachments to stay compatible with
    // the framebuffer begin_render_pass binds. Previous behaviour of
    // force-pushing RGBA8_UNorm produced a 1-color RP that mismatched a
    // 0-color framebuffer → vkCreateFramebuffer attachmentCount error.
    std::vector<PixelFormat> color_fmts = desc.color_formats;
    // Drop any `Undefined` entries — caller may have zero-initialised
    // slots we should not attach.
    color_fmts.erase(
        std::remove(color_fmts.begin(), color_fmts.end(),
                    PixelFormat::Undefined),
        color_fmts.end());
    bool needs_depth = desc.depth_format != PixelFormat::Undefined;
    auto t_renderpass0 = std::chrono::steady_clock::now();
    res.render_pass = get_or_create_render_pass(
        color_fmts, desc.depth_format, needs_depth, desc.sample_count,
        LoadOp::Clear, LoadOp::Clear);
    auto t_renderpass1 = std::chrono::steady_clock::now();
    g_pipeline_renderpass_us.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(t_renderpass1 - t_renderpass0).count(),
        std::memory_order_relaxed);

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    auto add_stage = [&](ShaderHandle h, VkShaderStageFlagBits vk_stage) {
        auto* sh = get_shader(h);
        if (!sh) return;
        VkPipelineShaderStageCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        si.stage = vk_stage;
        si.module = sh->module;
        si.pName = sh->entry_point.c_str();
        stages.push_back(si);
    };
    add_stage(desc.vertex_shader, VK_SHADER_STAGE_VERTEX_BIT);
    add_stage(desc.fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT);
    if (desc.geometry_shader) add_stage(desc.geometry_shader, VK_SHADER_STAGE_GEOMETRY_BIT);
    const VkShaderResource* vertex_shader = get_shader(desc.vertex_shader);

    // Vertex input
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    uint32_t reflected_attr_index = 0;
    for (uint32_t i = 0; i < desc.vertex_layouts.size(); ++i) {
        const auto& vl = desc.vertex_layouts[i];
        VkVertexInputBindingDescription bd{};
        bd.binding = i;
        bd.stride = vl.stride;
        bd.inputRate = vl.per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        bindings.push_back(bd);

        for (uint32_t attr_index = 0;
             attr_index < static_cast<uint32_t>(vl.attributes.size());
             ++attr_index) {
            const auto& attr = vl.attributes[attr_index];
            uint32_t location = attr.location;
            if (vl.use_shader_input_locations) {
                if (!vertex_shader || !vertex_shader->vertex_input_locations_known) {
                    tc_log(TC_LOG_ERROR,
                        "[VulkanRenderDevice] shader-owned vertex input layout requested "
                        "but reflection is unavailable: shader='%s' binding=%u "
                        "attribute_index=%u",
                        vertex_shader ? vertex_shader->debug_name.c_str() : "null",
                        i,
                        attr_index);
                    continue;
                }
                if (reflected_attr_index >= vertex_shader->vertex_input_locations.size()) {
                    tc_log(TC_LOG_ERROR,
                        "[VulkanRenderDevice] shader-owned vertex input layout has more "
                        "attributes than the shader entry point declares: shader='%s' "
                        "binding=%u attribute_index=%u reflected_attribute_index=%u "
                        "shader_locations=[%s]",
                        vertex_shader->debug_name.c_str(),
                        i,
                        attr_index,
                        reflected_attr_index,
                        join_u32s(vertex_shader->vertex_input_locations).c_str());
                    continue;
                }
                location = vertex_shader->vertex_input_locations[reflected_attr_index];
                ++reflected_attr_index;
            }
            if (!vertex_shader_uses_location(vertex_shader, location)) {
                continue;
            }
            VkVertexInputAttributeDescription ad{};
            ad.location = location;
            ad.binding = i;
            ad.format = vk::to_vk_vertex_format(attr.format);
            ad.offset = attr.offset;
            attributes.push_back(ad);
        }
    }

    if (vertex_shader && vertex_shader->vertex_input_locations_known) {
        std::vector<uint32_t> provided_locations;
        provided_locations.reserve(attributes.size());
        for (const auto& attr : attributes) {
            provided_locations.push_back(attr.location);
        }
        std::sort(provided_locations.begin(), provided_locations.end());
        provided_locations.erase(
            std::unique(provided_locations.begin(), provided_locations.end()),
            provided_locations.end());

        std::vector<uint32_t> missing_locations;
        for (uint32_t location : vertex_shader->vertex_input_locations) {
            if (!vertex_attributes_have_location(attributes, location)) {
                missing_locations.push_back(location);
            }
        }

        if (!missing_locations.empty()) {
            const std::string shader_name = vertex_shader->debug_name.empty()
                ? std::string("<unnamed vertex shader>")
                : vertex_shader->debug_name;
            tc_log(TC_LOG_ERROR,
                "[VulkanRenderDevice] vertex input mismatch before pipeline creation: "
                "shader='%s' shader_handle=%u entry='%s' requires_locations=[%s] "
                "provided_locations=[%s] missing_locations=[%s] raw_vertex_layouts={%s} "
                "effective_vk_attributes={%s}. This usually means the shader declares "
                "an input the mesh/drawable layout does not provide, or the pass filtered "
                "the layout more aggressively than the shader interface allows.",
                shader_name.c_str(),
                desc.vertex_shader.id,
                vertex_shader->entry_point.c_str(),
                join_u32s(vertex_shader->vertex_input_locations).c_str(),
                join_u32s(provided_locations).c_str(),
                join_u32s(missing_locations).c_str(),
                describe_vertex_layouts(desc.vertex_layouts).c_str(),
                describe_vk_vertex_attributes(attributes).c_str());
        }
    } else if (vertex_shader) {
        const std::string shader_name = vertex_shader->debug_name.empty()
            ? std::string("<unnamed vertex shader>")
            : vertex_shader->debug_name;
        tc_log(TC_LOG_WARN,
            "[VulkanRenderDevice] vertex shader input reflection unavailable: "
            "shader='%s' shader_handle=%u entry='%s'; Vulkan validation may be "
            "the first place to report vertex input mismatches.",
            shader_name.c_str(),
            desc.vertex_shader.id,
            vertex_shader->entry_point.c_str());
    }

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertex_input.pVertexBindingDescriptions = bindings.data();
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = vk::to_vk_topology(desc.topology);

    // Dynamic viewport/scissor. No VkPipelineViewportDepthClipControl —
    // we target Vulkan-native NDC Z ∈ [0,1] everywhere; OpenGL reaches
    // the same convention via glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE).
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = vk::to_vk_polygon_mode(desc.raster.polygon_mode);
    raster.cullMode = vk::to_vk_cull_mode(desc.raster.cull);
    raster.frontFace = vk::to_vk_front_face(desc.raster.front_face);
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable = desc.raster.depth_bias_enabled ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = desc.raster.depth_bias_constant;
    raster.depthBiasSlopeFactor = desc.raster.depth_bias_slope;
    raster.depthBiasClamp = desc.raster.depth_bias_clamp;

    // Multisample
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.sample_count);

    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = desc.depth_stencil.depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = desc.depth_stencil.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = vk::to_vk_compare(desc.depth_stencil.depth_compare);

    // Blend — one attachment per color output. Must match subpass's
    // colorAttachmentCount, else VUID-VkGraphicsPipelineCreateInfo-
    // renderPass-06041 fires and some drivers silently reject the draw
    // (shadow/depth-only passes were hit by this — 0 color attachments
    // in the subpass but attachmentCount=1 on the pipeline).
    std::vector<VkPipelineColorBlendAttachmentState> blend_atts(color_fmts.size());
    for (auto& ba : blend_atts) {
        ba.blendEnable = desc.blend.enabled ? VK_TRUE : VK_FALSE;
        ba.srcColorBlendFactor = vk::to_vk_blend_factor(desc.blend.src_color);
        ba.dstColorBlendFactor = vk::to_vk_blend_factor(desc.blend.dst_color);
        ba.colorBlendOp = vk::to_vk_blend_op(desc.blend.color_op);
        ba.srcAlphaBlendFactor = vk::to_vk_blend_factor(desc.blend.src_alpha);
        ba.dstAlphaBlendFactor = vk::to_vk_blend_factor(desc.blend.dst_alpha);
        ba.alphaBlendOp = vk::to_vk_blend_op(desc.blend.alpha_op);
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = static_cast<uint32_t>(blend_atts.size());
    blend.pAttachments = blend_atts.empty() ? nullptr : blend_atts.data();

    // Create pipeline
    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = static_cast<uint32_t>(stages.size());
    pi.pStages = stages.data();
    pi.pVertexInputState = &vertex_input;
    pi.pInputAssemblyState = &input_assembly;
    pi.pViewportState = &viewport_state;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState = &multisample;
    pi.pDepthStencilState = &depth_stencil;
    pi.pColorBlendState = &blend;
    pi.pDynamicState = &dynamic_state;
    pi.layout = res.layout;
    pi.renderPass = res.render_pass;
    pi.subpass = 0;

    auto t_pipeline0 = std::chrono::steady_clock::now();
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &res.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }
    auto t_pipeline1 = std::chrono::steady_clock::now();
    g_pipeline_create_us.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(t_pipeline1 - t_pipeline0).count(),
        std::memory_order_relaxed);
    g_pipeline_count.fetch_add(1, std::memory_order_relaxed);

    return {pipelines_.add(std::move(res))};
}

// --- Resource set ---

namespace {

struct DynSlot {
    uint32_t binding = 0;
    uint32_t offset = 0;
};

static uint64_t hash_resolved_resource_bindings(
    uintptr_t resource_layout_token,
    std::span<const VulkanResolvedResourceBinding> bindings,
    uint64_t domain
) {
    // FNV-1a over stable descriptor identity. Dynamic UBO offsets are
    // intentionally excluded because they are supplied to
    // vkCmdBindDescriptorSets as dynamic offsets, not descriptor writes.
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 0x100000001b3ull;
    };
    mix(domain);
    for (const VulkanResolvedResourceBinding& b : bindings) {
        mix(b.binding);
        mix(b.array_element);
        mix(static_cast<uint64_t>(b.expected_descriptor_type));
        mix(static_cast<uint64_t>(b.kind));
        mix(b.buffer.id);
        if (b.expected_descriptor_type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
            mix(b.offset);
        }
        mix(b.range);
        mix(b.texture.id);
        mix(b.sampler.id);
    }
    mix(resource_layout_token);
    return h;
}

static VkDescriptorType legacy_expected_descriptor_type(ResourceBinding::Kind kind) {
    switch (kind) {
        case ResourceBinding::Kind::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case ResourceBinding::Kind::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case ResourceBinding::Kind::SampledTexture:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case ResourceBinding::Kind::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

static BoundResourceKind bound_kind_from_legacy(ResourceBinding::Kind kind) {
    switch (kind) {
        case ResourceBinding::Kind::UniformBuffer:
            return BoundResourceKind::UniformBuffer;
        case ResourceBinding::Kind::StorageBuffer:
            return BoundResourceKind::StorageBuffer;
        case ResourceBinding::Kind::SampledTexture:
            return BoundResourceKind::SampledTexture;
        case ResourceBinding::Kind::Sampler:
            return BoundResourceKind::Sampler;
    }
    return BoundResourceKind::UniformBuffer;
}

static ResourceBinding::Kind legacy_kind_from_bound(BoundResourceKind kind) {
    switch (kind) {
        case BoundResourceKind::UniformBuffer:
            return ResourceBinding::Kind::UniformBuffer;
        case BoundResourceKind::StorageBuffer:
            return ResourceBinding::Kind::StorageBuffer;
        case BoundResourceKind::SampledTexture:
            return ResourceBinding::Kind::SampledTexture;
        case BoundResourceKind::Sampler:
            return ResourceBinding::Kind::Sampler;
    }
    return ResourceBinding::Kind::UniformBuffer;
}

static bool descriptor_kind_matches_layout(
    BackendDescriptorKind descriptor_kind,
    VkDescriptorType layout_type
) {
    switch (descriptor_kind) {
        case BackendDescriptorKind::UniformBuffer:
            return layout_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                   layout_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case BackendDescriptorKind::StorageBuffer:
            return layout_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case BackendDescriptorKind::SampledTexture:
            return layout_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                   layout_type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case BackendDescriptorKind::Sampler:
            return layout_type == VK_DESCRIPTOR_TYPE_SAMPLER;
        case BackendDescriptorKind::StorageTexture:
            return layout_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case BackendDescriptorKind::None:
        default:
            return false;
    }
}

static VkDescriptorType expected_descriptor_type_from_layout(
    BackendDescriptorKind descriptor_kind,
    VkDescriptorType layout_type
) {
    if (!descriptor_kind_matches_layout(descriptor_kind, layout_type)) {
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
    return layout_type;
}

static bool bound_value_kind_matches_descriptor_kind(
    BoundResourceKind value_kind,
    BackendDescriptorKind descriptor_kind
) {
    switch (descriptor_kind) {
        case BackendDescriptorKind::UniformBuffer:
            return value_kind == BoundResourceKind::UniformBuffer;
        case BackendDescriptorKind::StorageBuffer:
            return value_kind == BoundResourceKind::StorageBuffer;
        case BackendDescriptorKind::SampledTexture:
            return value_kind == BoundResourceKind::SampledTexture;
        case BackendDescriptorKind::Sampler:
            return value_kind == BoundResourceKind::Sampler;
        case BackendDescriptorKind::StorageTexture:
        case BackendDescriptorKind::None:
        default:
            return false;
    }
}

static const VkDescriptorSetLayoutBinding* find_layout_binding(
    const std::vector<VkDescriptorSetLayoutBinding>& layout_bindings,
    uint32_t binding
) {
    for (const VkDescriptorSetLayoutBinding& lb : layout_bindings) {
        if (lb.binding == binding) return &lb;
    }
    return nullptr;
}

static void sort_resolved_bindings(std::vector<VulkanResolvedResourceBinding>& bindings) {
    std::sort(bindings.begin(), bindings.end(),
        [](const VulkanResolvedResourceBinding& a, const VulkanResolvedResourceBinding& b) {
            if (a.binding != b.binding) return a.binding < b.binding;
            return a.array_element < b.array_element;
        });
}

} // namespace

ResourceSetHandle VulkanRenderDevice::create_resolved_resource_set(
    VkDescriptorSetLayout layout,
    const std::vector<VkDescriptorSetLayoutBinding>& layout_bindings,
    uintptr_t resource_layout_token,
    std::span<const VulkanResolvedResourceBinding> resolved_bindings,
    VkResourceSetResource res,
    uint64_t cache_domain
) {
    bool has_ring = false;
    if (ring_ubo_handle_) {
        for (const VulkanResolvedResourceBinding& b : resolved_bindings) {
            if (b.buffer == ring_ubo_handle_) {
                has_ring = true;
                break;
            }
        }
    }

    const uint64_t key = hash_resolved_resource_bindings(
        resource_layout_token,
        resolved_bindings,
        cache_domain);
    auto& cache = descriptor_cache_[current_pool_idx_];
    if (!has_ring) {
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
    }

    g_resource_set_count.fetch_add(1, std::memory_order_relaxed);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = descriptor_pools_[current_pool_idx_];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;

    VkResult alloc_result = vkAllocateDescriptorSets(device_, &ai, &res.descriptor_set);
    if (alloc_result != VK_SUCCESS) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice: vkAllocateDescriptorSets failed result=%d pool=%u cache_size=%zu bindings=%zu",
               static_cast<int>(alloc_result),
               current_pool_idx_,
               cache.size(),
               resolved_bindings.size());
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> buf_infos;
    std::vector<VkDescriptorImageInfo> img_infos;
    size_t layout_descriptor_count = 0;
    for (const VkDescriptorSetLayoutBinding& lb : layout_bindings) {
        layout_descriptor_count += std::max(lb.descriptorCount, 1u);
    }
    buf_infos.reserve(layout_descriptor_count);
    img_infos.reserve(layout_descriptor_count);

    std::vector<DynSlot> dyn_slots;

    auto find_resolved_binding = [&](uint32_t binding, uint32_t array_element)
        -> const VulkanResolvedResourceBinding* {
        for (const VulkanResolvedResourceBinding& b : resolved_bindings) {
            if (b.binding == binding && b.array_element == array_element) {
                return &b;
            }
        }
        return nullptr;
    };

    for (const VkDescriptorSetLayoutBinding& lb : layout_bindings) {
        const uint32_t descriptor_count = std::max(lb.descriptorCount, 1u);
        for (uint32_t element = 0; element < descriptor_count; ++element) {
            const VulkanResolvedResourceBinding* provided =
                find_resolved_binding(lb.binding, element);

            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = res.descriptor_set;
            w.dstBinding = lb.binding;
            w.dstArrayElement = element;
            w.descriptorCount = 1;
            w.descriptorType = lb.descriptorType;

            switch (lb.descriptorType) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: {
                    BufferHandle buffer = ring_ubo_handle_;
                    uint64_t offset = 0;
                    uint64_t range = 16;
                    if (provided && provided->kind == BoundResourceKind::UniformBuffer) {
                        buffer = provided->buffer;
                        offset = provided->offset;
                        range = provided->range;
                    }
                    auto* buf = get_buffer(buffer);
                    if (!buf) continue;
                    constexpr uint64_t VK_MIN_MAX_UBO_RANGE = 65536;
                    if (range == 0)
                        range = std::min<uint64_t>(buf->desc.size, VK_MIN_MAX_UBO_RANGE);
                    if (range > VK_MIN_MAX_UBO_RANGE) range = VK_MIN_MAX_UBO_RANGE;
                    const bool dynamic_ubo =
                        lb.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    const bool is_ring = ring_ubo_handle_ && (buffer == ring_ubo_handle_);
                    buf_infos.push_back({
                        buf->buffer,
                        dynamic_ubo ? 0u : offset,
                        range});
                    w.pBufferInfo = &buf_infos.back();
                    if (dynamic_ubo) {
                        dyn_slots.push_back({
                            lb.binding,
                            is_ring ? static_cast<uint32_t>(offset) : 0u});
                    }
                    break;
                }
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
                    if (!provided || provided->kind != BoundResourceKind::StorageBuffer) {
                        continue;
                    }
                    auto* buf = get_buffer(provided->buffer);
                    if (!buf) continue;
                    buf_infos.push_back({
                        buf->buffer,
                        provided->offset,
                        provided->range ? provided->range : VK_WHOLE_SIZE});
                    w.pBufferInfo = &buf_infos.back();
                    break;
                }
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                    TextureHandle texture = ensure_default_sampled_texture();
                    SamplerHandle sampler{};
                    if (provided && provided->kind == BoundResourceKind::SampledTexture) {
                        texture = provided->texture;
                        sampler = provided->sampler;
                    }
                    auto* tex = get_texture(texture);
                    if (!tex) continue;
                    VkSampler samp_vk = VK_NULL_HANDLE;
                    if (sampler) {
                        auto* samp = get_sampler(sampler);
                        if (samp) samp_vk = samp->sampler;
                    }
                    if (samp_vk == VK_NULL_HANDLE) samp_vk = ensure_default_sampler();
                    img_infos.push_back({
                        samp_vk,
                        tex->view,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                    w.pImageInfo = &img_infos.back();
                    break;
                }
                case VK_DESCRIPTOR_TYPE_SAMPLER: {
                    VkSampler samp_vk = ensure_default_sampler();
                    if (provided && provided->kind == BoundResourceKind::Sampler) {
                        auto* samp = get_sampler(provided->sampler);
                        if (samp) samp_vk = samp->sampler;
                    }
                    img_infos.push_back({
                        samp_vk,
                        VK_NULL_HANDLE,
                        VK_IMAGE_LAYOUT_UNDEFINED});
                    w.pImageInfo = &img_infos.back();
                    break;
                }
                default:
                    continue;
            }
            writes.push_back(w);
        }
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(
            device_,
            static_cast<uint32_t>(writes.size()),
            writes.data(),
            0,
            nullptr);
    }

    std::sort(dyn_slots.begin(), dyn_slots.end(),
        [](const DynSlot& a, const DynSlot& b) { return a.binding < b.binding; });
    res.dynamic_offset_count = static_cast<uint32_t>(std::min(
        dyn_slots.size(), static_cast<size_t>(VkResourceSetResource::MAX_DYNAMIC_OFFSETS)));
    for (uint32_t i = 0; i < res.dynamic_offset_count; ++i) {
        res.dynamic_offsets[i] = dyn_slots[i].offset;
    }

    const bool cache_owned = !has_ring;
    res.descriptor_cache_owned = cache_owned;
    ResourceSetHandle handle{resource_sets_.add(std::move(res))};
    if (cache_owned) cache[key] = handle;
    return handle;
}

ResourceSetHandle VulkanRenderDevice::create_resource_set(const ResourceSetDesc& desc) {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (desc.effective_resource_layout_token() != 0) {
        layout = reinterpret_cast<VkDescriptorSetLayout>(
            desc.effective_resource_layout_token());
    }
    if (!layout) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice: create_resource_set called without resource_layout_token");
        return {};
    }

    const auto layout_it = descriptor_layout_bindings_.find(layout);
    if (layout_it == descriptor_layout_bindings_.end()) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice: create_resource_set called with unknown resource_layout_token=%p",
               static_cast<void*>(layout));
        return {};
    }
    const std::vector<VkDescriptorSetLayoutBinding>& layout_bindings = layout_it->second;

    std::vector<VulkanResolvedResourceBinding> resolved_bindings;
    resolved_bindings.reserve(desc.bindings.size());
    for (const ResourceBinding& b : desc.bindings) {
        const VkDescriptorSetLayoutBinding* lb =
            find_layout_binding(layout_bindings, b.binding);
        if (!lb) {
            continue;
        }
        const VkDescriptorType expected = legacy_expected_descriptor_type(b.kind);
        if (lb->descriptorType != expected) {
            tc_log(TC_LOG_WARN,
                   "VulkanRenderDevice: skipping resource binding %u[%u], descriptor type mismatch layout=%u binding=%u",
                   b.binding,
                   b.array_element,
                   static_cast<unsigned>(lb->descriptorType),
                   static_cast<unsigned>(expected));
            continue;
        }
        if (b.array_element >= lb->descriptorCount) {
            tc_log(TC_LOG_WARN,
                   "VulkanRenderDevice: skipping resource binding %u[%u], descriptor array count is %u",
                   b.binding,
                   b.array_element,
                   lb->descriptorCount);
            continue;
        }
        VulkanResolvedResourceBinding value;
        value.binding = b.binding;
        value.array_element = b.array_element;
        value.expected_descriptor_type = expected;
        value.kind = bound_kind_from_legacy(b.kind);
        value.buffer = b.buffer;
        value.texture = b.texture;
        value.sampler = b.sampler;
        value.offset = b.offset;
        value.range = b.range;
        resolved_bindings.push_back(value);
    }
    sort_resolved_bindings(resolved_bindings);

    VkResourceSetResource res;
    res.desc = desc;
    ResourceSetDesc sorted_desc = desc;
    sorted_desc.bindings.clear();
    sorted_desc.bindings.reserve(resolved_bindings.size());
    for (const VulkanResolvedResourceBinding& value : resolved_bindings) {
        ResourceBinding b;
        b.binding = value.binding;
        b.array_element = value.array_element;
        b.kind = legacy_kind_from_bound(value.kind);
        b.buffer = value.buffer;
        b.texture = value.texture;
        b.sampler = value.sampler;
        b.offset = value.offset;
        b.range = value.range;
        sorted_desc.bindings.push_back(b);
    }
    res.desc = sorted_desc;

    return create_resolved_resource_set(
        layout,
        layout_bindings,
        desc.effective_resource_layout_token(),
        resolved_bindings,
        std::move(res),
        0x6c65676163790001ull);
}

ResourceSetHandle VulkanRenderDevice::create_bound_resource_set(
    const BoundResourceSetDesc& desc,
    const std::vector<ResourceBinding>& legacy_numeric_bindings
) {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (desc.resource_layout_token != 0) {
        layout = reinterpret_cast<VkDescriptorSetLayout>(desc.resource_layout_token);
    }
    if (!layout) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice: create_bound_resource_set called without resource_layout_token");
        return {};
    }

    const auto layout_it = descriptor_layout_bindings_.find(layout);
    if (layout_it == descriptor_layout_bindings_.end()) {
        tc_log(TC_LOG_ERROR,
               "VulkanRenderDevice: create_bound_resource_set called with unknown resource_layout_token=%p",
               static_cast<void*>(layout));
        return {};
    }
    const std::vector<VkDescriptorSetLayoutBinding>& layout_bindings = layout_it->second;

    std::vector<VulkanResolvedResourceBinding> resolved_bindings;
    resolved_bindings.reserve(
        legacy_numeric_bindings.size() + bound_resource_binding_count(desc));

    for (const ResourceBinding& b : legacy_numeric_bindings) {
        const VkDescriptorSetLayoutBinding* lb =
            find_layout_binding(layout_bindings, b.binding);
        if (!lb) {
            continue;
        }
        const VkDescriptorType expected = legacy_expected_descriptor_type(b.kind);
        if (lb->descriptorType != expected || b.array_element >= lb->descriptorCount) {
            continue;
        }
        VulkanResolvedResourceBinding value;
        value.binding = b.binding;
        value.array_element = b.array_element;
        value.expected_descriptor_type = expected;
        value.kind = bound_kind_from_legacy(b.kind);
        value.buffer = b.buffer;
        value.texture = b.texture;
        value.sampler = b.sampler;
        value.offset = b.offset;
        value.range = b.range;
        resolved_bindings.push_back(value);
    }

    bool bound_ok = true;
    for_each_bound_resource_binding(desc, [&](const BoundResourceBinding& b) {
        if (!bound_ok) {
            return;
        }
        const BackendBoundResourceSlot& slot = b.slot;
        if (slot.placement.kind != BackendPlacementKind::VulkanDescriptor) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice: resource '%s' has non-Vulkan placement kind=%u",
                   bound_resource_debug_name(b),
                   static_cast<unsigned>(slot.placement.kind));
            bound_ok = false;
            return;
        }
        if (slot.placement.vulkan.set != 0) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice: resource '%s' uses unsupported descriptor set=%u",
                   bound_resource_debug_name(b),
                   slot.placement.vulkan.set);
            bound_ok = false;
            return;
        }
        const uint32_t binding = slot.placement.vulkan.binding;
        const VkDescriptorSetLayoutBinding* lb =
            find_layout_binding(layout_bindings, binding);
        if (!lb) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice: resource '%s' references missing layout binding=%u",
                   bound_resource_debug_name(b),
                   binding);
            bound_ok = false;
            return;
        }
        if (b.value.array_element >= lb->descriptorCount) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice: resource '%s' array element=%u exceeds descriptor count=%u at binding=%u",
                   bound_resource_debug_name(b),
                   b.value.array_element,
                   lb->descriptorCount,
                   binding);
            bound_ok = false;
            return;
        }
        const VkDescriptorType expected = expected_descriptor_type_from_layout(
            slot.placement.vulkan.descriptor_kind,
            lb->descriptorType);
        if (expected == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice: resource '%s' descriptor kind=%u does not match layout descriptor type=%u at binding=%u",
                   bound_resource_debug_name(b),
                   static_cast<unsigned>(slot.placement.vulkan.descriptor_kind),
                   static_cast<unsigned>(lb->descriptorType),
                   binding);
            bound_ok = false;
            return;
        }
        if (!bound_value_kind_matches_descriptor_kind(
                b.value.kind,
                slot.placement.vulkan.descriptor_kind)) {
            tc_log(TC_LOG_ERROR,
                   "VulkanRenderDevice: resource '%s' value kind=%u does not match descriptor kind=%u at binding=%u",
                   bound_resource_debug_name(b),
                   static_cast<unsigned>(b.value.kind),
                   static_cast<unsigned>(slot.placement.vulkan.descriptor_kind),
                   binding);
            bound_ok = false;
            return;
        }

        VulkanResolvedResourceBinding value;
        value.binding = binding;
        value.array_element = b.value.array_element;
        value.expected_descriptor_type = expected;
        value.kind = b.value.kind;
        value.buffer = b.value.buffer;
        value.texture = b.value.texture;
        value.sampler = b.value.sampler;
        value.offset = b.value.offset;
        value.range = b.value.range;
        resolved_bindings.push_back(value);
    });
    if (!bound_ok) {
        return {};
    }
    sort_resolved_bindings(resolved_bindings);

    VkResourceSetResource res;
    res.bound_desc = desc;
    res.legacy_numeric_bindings = legacy_numeric_bindings;
    res.has_bound_desc = true;

    return create_resolved_resource_set(
        layout,
        layout_bindings,
        desc.resource_layout_token,
        resolved_bindings,
        std::move(res),
        0x626f756e64000001ull);
}

} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
