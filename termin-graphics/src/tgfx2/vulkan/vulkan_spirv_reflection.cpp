#ifdef TGFX2_HAS_VULKAN

#include "vulkan_spirv_reflection.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

extern "C" {
#include <tcbase/tc_log.h>
}

namespace tgfx {

namespace {

uint32_t spirv_execution_model_for_stage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:
            return 0; // Vertex
        case ShaderStage::Fragment:
            return 4; // Fragment
        case ShaderStage::Geometry:
            return 3; // Geometry
        case ShaderStage::Compute:
            return 5; // GLCompute
    }
    return UINT32_MAX;
}

std::string spirv_read_string(const uint32_t* words, uint32_t word_count, uint32_t start_word) {
    std::string out;
    for (uint32_t i = start_word; i < word_count; ++i) {
        uint32_t word = words[i];
        for (uint32_t b = 0; b < 4; ++b) {
            char ch = static_cast<char>((word >> (8u * b)) & 0xffu);
            if (ch == '\0') return out;
            out.push_back(ch);
        }
    }
    return out;
}

uint32_t spirv_string_word_count(const std::string& text) {
    return static_cast<uint32_t>((text.size() + 4u) / 4u);
}



} // namespace

std::string reflect_spirv_stage_entry_point(
    const std::vector<uint32_t>& spirv,
    ShaderStage stage
) {
    if (spirv.size() < 5) return {};

    static constexpr uint32_t OP_ENTRY_POINT = 15;
    const uint32_t execution_model = spirv_execution_model_for_stage(stage);
    if (execution_model == UINT32_MAX) return {};

    for (uint32_t offset = 5; offset < spirv.size();) {
        uint32_t op_word = spirv[offset];
        uint32_t word_count = op_word >> 16u;
        uint32_t opcode = op_word & 0xffffu;
        if (word_count == 0 || offset + word_count > spirv.size()) return {};

        const uint32_t* words = spirv.data() + offset;
        if (opcode == OP_ENTRY_POINT && word_count >= 3 && words[1] == execution_model) {
            return spirv_read_string(words, word_count, 3);
        }

        offset += word_count;
    }

    return {};
}

SpirvVertexInputs reflect_spirv_vertex_inputs(
    const std::vector<uint32_t>& spirv,
    const std::string& entry_point
) {
    SpirvVertexInputs result;
    if (spirv.size() < 5) return result;

    static constexpr uint32_t OP_ENTRY_POINT = 15;
    static constexpr uint32_t OP_DECORATE = 71;
    static constexpr uint32_t OP_VARIABLE = 59;
    static constexpr uint32_t EXECUTION_MODEL_VERTEX = 0;
    static constexpr uint32_t STORAGE_CLASS_INPUT = 1;
    static constexpr uint32_t DECORATION_LOCATION = 30;

    std::vector<uint32_t> vertex_entry_interfaces;
    std::unordered_set<uint32_t> vertex_entry_interface_seen;
    std::unordered_map<uint32_t, uint32_t> storage_class_by_id;
    std::unordered_map<uint32_t, uint32_t> location_by_id;

    for (uint32_t offset = 5; offset < spirv.size();) {
        uint32_t op_word = spirv[offset];
        uint32_t word_count = op_word >> 16u;
        uint32_t opcode = op_word & 0xffffu;
        if (word_count == 0 || offset + word_count > spirv.size()) return result;

        const uint32_t* words = spirv.data() + offset;

        if (opcode == OP_ENTRY_POINT && word_count >= 3 && words[1] == EXECUTION_MODEL_VERTEX) {
            std::string name = spirv_read_string(words, word_count, 3);
            if (name == entry_point) {
                uint32_t first_interface = 3u + spirv_string_word_count(name);
                for (uint32_t i = first_interface; i < word_count; ++i) {
                    if (vertex_entry_interface_seen.insert(words[i]).second) {
                        vertex_entry_interfaces.push_back(words[i]);
                    }
                }
                result.known = true;
            }
        } else if (opcode == OP_DECORATE && word_count >= 4 && words[2] == DECORATION_LOCATION) {
            location_by_id[words[1]] = words[3];
        } else if (opcode == OP_VARIABLE && word_count >= 4) {
            storage_class_by_id[words[2]] = words[3];
        }

        offset += word_count;
    }

    if (!result.known) return result;

    for (uint32_t id : vertex_entry_interfaces) {
        auto storage_it = storage_class_by_id.find(id);
        if (storage_it == storage_class_by_id.end() || storage_it->second != STORAGE_CLASS_INPUT) {
            continue;
        }
        auto location_it = location_by_id.find(id);
        if (location_it != location_by_id.end()) {
            result.locations.push_back(location_it->second);
        }
    }
    return result;
}

// Reflect descriptor bindings from SPIR-V bytecode.
// Walks OpDecorate (DescriptorSet=0, Binding=N) → OpType*, OpVariable
// to determine (binding, VkDescriptorType, count) per resource.
std::vector<VkShaderResource::DescriptorBinding> reflect_spirv_descriptor_bindings(
    const std::vector<uint32_t>& spirv)
{
    std::vector<VkShaderResource::DescriptorBinding> result;
    if (spirv.size() < 5) return result;

    static constexpr uint32_t OP_DECORATE = 71;
    static constexpr uint32_t OP_VARIABLE = 59;
    static constexpr uint32_t OP_TYPE_IMAGE = 25;
    static constexpr uint32_t OP_TYPE_SAMPLER = 26;
    static constexpr uint32_t OP_TYPE_SAMPLED_IMAGE = 27;
    static constexpr uint32_t OP_TYPE_STRUCT = 30;
    static constexpr uint32_t OP_TYPE_ARRAY = 28;
    static constexpr uint32_t OP_TYPE_POINTER = 32;
    static constexpr uint32_t DECORATION_DESCRIPTOR_SET = 3;
    static constexpr uint32_t DECORATION_BINDING = 33;
    static constexpr uint32_t DECORATION_BLOCK = 2;
    static constexpr uint32_t STORAGE_CLASS_UNIFORM = 2;
    static constexpr uint32_t STORAGE_CLASS_STORAGE_BUFFER = 12;

    // Build tables: a variable's descriptor set & binding, type chain.
    std::unordered_map<uint32_t, uint32_t> desc_set_by_id;    // var_id → set
    std::unordered_map<uint32_t, uint32_t> binding_by_id;     // var_id → binding
    std::unordered_map<uint32_t, uint32_t> var_type_id;       // var_id → result_type_id
    std::unordered_map<uint32_t, uint32_t> var_storage_class; // var_id → storage_class
    std::unordered_map<uint32_t, uint32_t> pointer_pointee;   // pointer_type_id → pointee_type_id
    std::unordered_map<uint32_t, uint32_t> array_elem_type;   // array_type_id → element_type_id
    std::unordered_map<uint32_t, uint32_t> array_length;      // array_type_id → length
    std::unordered_set<uint32_t> block_structs;               // struct decorated with Block
    std::unordered_map<uint32_t, uint32_t> image_sampled;     // image_type_id → sampled (0=unknown,1=yes,2=no)
    std::unordered_map<uint32_t, uint32_t> type_opcode;       // type_id → opcode

    for (uint32_t offset = 5; offset < spirv.size();) {
        uint32_t op_word = spirv[offset];
        uint32_t word_count = op_word >> 16u;
        uint32_t opcode = op_word & 0xffffu;
        if (word_count == 0 || offset + word_count > spirv.size()) break;

        const uint32_t* words = spirv.data() + offset;

        if (opcode == OP_DECORATE && word_count >= 3) {
            uint32_t target_id = words[1];
            uint32_t decoration = words[2];
            if (decoration == DECORATION_DESCRIPTOR_SET && word_count >= 4) {
                if (words[3] != 0) {
                    tc_log(TC_LOG_ERROR,
                           "VulkanRenderDevice: SPIR-V decoration DescriptorSet=%u != 0 "
                           "on target %u; only set=0 is supported",
                           words[3], target_id);
                }
                desc_set_by_id[target_id] = words[3];
            } else if (decoration == DECORATION_BINDING && word_count >= 4) {
                binding_by_id[target_id] = words[3];
            } else if (decoration == DECORATION_BLOCK) {
                block_structs.insert(target_id);
            }
        } else if (opcode == OP_VARIABLE && word_count >= 4) {
            var_type_id[words[2]] = words[1];
            var_storage_class[words[2]] = words[3];
        } else if (opcode == OP_TYPE_POINTER && word_count >= 4) {
            type_opcode[words[1]] = opcode;
            pointer_pointee[words[1]] = words[3];
        } else if (opcode == OP_TYPE_ARRAY && word_count >= 4) {
            type_opcode[words[1]] = opcode;
            array_elem_type[words[1]] = words[2];
            // Constant for array length — usually OpConstant, word 3 is the constant id
            // We need to resolve it. Simplification: look up OpConstant.
        } else if (opcode == OP_TYPE_IMAGE && word_count >= 8) {
            type_opcode[words[1]] = opcode;
            image_sampled[words[1]] = words[7];  // Sampled operand
        } else {
            // Record type opcodes for type-chain walking.
            if (opcode >= OP_TYPE_IMAGE && opcode <= OP_TYPE_POINTER) {
                type_opcode[words[1]] = opcode;
            }
        }

        offset += word_count;
    }

    // Resolve array lengths from OpConstant.
    std::unordered_map<uint32_t, uint32_t> constant_values;
    for (uint32_t offset = 5; offset < spirv.size();) {
        uint32_t op_word = spirv[offset];
        uint32_t word_count = op_word >> 16u;
        uint32_t opcode = op_word & 0xffffu;
        if (word_count == 0 || offset + word_count > spirv.size()) break;
        const uint32_t* words = spirv.data() + offset;
        // OpConstant %type %id %value  (word_count >= 4)
        static constexpr uint32_t OP_CONSTANT = 43;
        if (opcode == OP_CONSTANT && word_count >= 4) {
            constant_values[words[2]] = words[3];
        }
        offset += word_count;
    }
    // Populate array lengths from the type table.
    for (uint32_t offset = 5; offset < spirv.size();) {
        uint32_t op_word = spirv[offset];
        uint32_t word_count = op_word >> 16u;
        uint32_t opcode = op_word & 0xffffu;
        if (word_count == 0 || offset + word_count > spirv.size()) break;
        const uint32_t* words = spirv.data() + offset;
        if (opcode == OP_TYPE_ARRAY && word_count >= 4) {
            auto it = constant_values.find(words[3]);
            if (it != constant_values.end()) {
                array_length[words[1]] = it->second;
            }
        }
        offset += word_count;
    }

    // Walk variables with binding decorations.
    for (const auto& [var_id, binding] : binding_by_id) {

        auto type_it = var_type_id.find(var_id);
        if (type_it == var_type_id.end()) continue;

        // Follow pointer → pointee type chain.
        uint32_t type_id = type_it->second;
        // Unwrap pointer.
        auto ptr_it = pointer_pointee.find(type_id);
        if (ptr_it != pointer_pointee.end()) {
            type_id = ptr_it->second;
        }

        // Determine descriptor type from the pointee.
        VkDescriptorType desc_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uint32_t count = 1;
        const uint32_t storage_class =
            var_storage_class.count(var_id) ? var_storage_class[var_id] : STORAGE_CLASS_UNIFORM;

        // Check for array.
        auto arr_elem_it = array_elem_type.find(type_id);
        auto arr_len_it = array_length.find(type_id);
        if (arr_elem_it != array_elem_type.end()) {
            // Unwrap array to get the element type.
            if (arr_len_it != array_length.end()) {
                count = arr_len_it->second;
            }
            type_id = arr_elem_it->second;
            // Follow pointer inside array element if needed.
            auto inner_ptr = pointer_pointee.find(type_id);
            if (inner_ptr != pointer_pointee.end()) {
                type_id = inner_ptr->second;
            }
        }

        auto op_it = type_opcode.find(type_id);
        if (op_it != type_opcode.end()) {
            switch (op_it->second) {
                case OP_TYPE_STRUCT:
                    desc_type = (storage_class == STORAGE_CLASS_STORAGE_BUFFER)
                        ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                        : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    break;
                case OP_TYPE_SAMPLED_IMAGE:
                    desc_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    break;
                case OP_TYPE_SAMPLER:
                    desc_type = VK_DESCRIPTOR_TYPE_SAMPLER;
                    break;
                case OP_TYPE_IMAGE: {
                    auto samp_it = image_sampled.find(type_id);
                    desc_type = (samp_it != image_sampled.end() && samp_it->second == 2)
                        ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                        : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    break;
                }
                default:
                    continue;  // unknown type, skip
            }
        } else {
            continue;  // can't determine type
        }

        VkShaderResource::DescriptorBinding db;
        db.binding = binding;
        db.descriptor_type = desc_type;
        db.count = count;
        result.push_back(db);
    }

    // Deduplicate by binding.
    std::sort(result.begin(), result.end(),
        [](const VkShaderResource::DescriptorBinding& a,
           const VkShaderResource::DescriptorBinding& b) {
            return a.binding < b.binding;
        });
    result.erase(
        std::unique(result.begin(), result.end(),
            [](const VkShaderResource::DescriptorBinding& a,
               const VkShaderResource::DescriptorBinding& b) {
                return a.binding == b.binding;
            }),
        result.end());

    return result;
}

bool vertex_shader_uses_location(const VkShaderResource* shader, uint32_t location) {
    if (!shader || !shader->vertex_input_locations_known) return true;
    return std::find(shader->vertex_input_locations.begin(),
                     shader->vertex_input_locations.end(),
                     location) != shader->vertex_input_locations.end();
}

bool vertex_attributes_have_location(
    const std::vector<VkVertexInputAttributeDescription>& attributes,
    uint32_t location
) {
    return std::any_of(attributes.begin(), attributes.end(),
        [location](const VkVertexInputAttributeDescription& attr) {
            return attr.location == location;
        });
}

std::string join_u32s(const std::vector<uint32_t>& values) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ", ";
        out << values[i];
    }
    return out.str();
}

std::string describe_vk_vertex_attributes(
    const std::vector<VkVertexInputAttributeDescription>& attributes
) {
    std::ostringstream out;
    for (size_t i = 0; i < attributes.size(); ++i) {
        const auto& attr = attributes[i];
        if (i) out << "; ";
        out << "loc=" << attr.location
            << " binding=" << attr.binding
            << " offset=" << attr.offset
            << " vkfmt=" << attr.format;
    }
    return out.str();
}

std::string describe_vertex_layouts(
    const std::vector<VertexLayoutDesc>& layouts
) {
    std::ostringstream out;
    for (size_t i = 0; i < layouts.size(); ++i) {
        const auto& layout = layouts[i];
        if (i) out << " | ";
        out << "binding " << i
            << " stride=" << layout.stride
            << " rate=" << (layout.per_instance ? "instance" : "vertex")
            << " attrs=[";
        const uint32_t attribute_count = std::min(
            layout.attribute_count,
            TGFX2_VERTEX_ATTRIBUTE_MAX);
        for (uint32_t j = 0; j < attribute_count; ++j) {
            const VertexAttributeDesc& attr = layout.attributes[j];
            if (j) out << "; ";
            out << "loc=" << attr.location
                << " offset=" << attr.offset
                << " fmt=" << static_cast<int>(attr.format);
        }
        out << "]";
    }
    return out.str();
}



} // namespace tgfx

#endif // TGFX2_HAS_VULKAN
