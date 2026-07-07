#include "tgfx2/vertex_layout.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>

#include <tcbase/tc_log.hpp>
#include <tcbase/tc_string.h>

namespace tgfx {
namespace {

void hash_combine(size_t& seed, size_t v) {
    seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

std::string_view semantic_view(const char* semantic) {
    if (!semantic || semantic[0] == '\0') {
        return {};
    }
    return semantic;
}

bool semantic_equal(const char* a, const char* b) {
    if (a == b) {
        return true;
    }
    const std::string_view av = semantic_view(a);
    const std::string_view bv = semantic_view(b);
    return av == bv;
}

} // namespace

const char* intern_vertex_semantic(const char* semantic) {
    if (!semantic || semantic[0] == '\0') {
        return nullptr;
    }
    return tc_intern_string(semantic);
}

const char* intern_vertex_semantic(std::string_view semantic) {
    if (semantic.empty()) {
        return nullptr;
    }
    if (semantic.back() == '\0') {
        return intern_vertex_semantic(semantic.data());
    }
    return tc_intern_string(std::string(semantic).c_str());
}

VertexLayoutDesc make_vertex_layout_desc(const VertexBufferLayout& layout) {
    VertexLayoutDesc desc;
    desc.stride = layout.stride;
    desc.per_instance = layout.per_instance;
    desc.use_shader_input_locations = layout.use_shader_input_locations;
    const size_t count = std::min<size_t>(
        layout.attributes.size(),
        TGFX2_VERTEX_ATTRIBUTE_MAX);
    desc.attribute_count = static_cast<uint32_t>(count);
    if (layout.attributes.size() > TGFX2_VERTEX_ATTRIBUTE_MAX) {
        tc::Log::error(
            "make_vertex_layout_desc: layout has %zu attributes, max is %u; truncating",
            layout.attributes.size(),
            TGFX2_VERTEX_ATTRIBUTE_MAX);
    }
    for (size_t i = 0; i < count; ++i) {
        const VertexAttribute& attr = layout.attributes[i];
        desc.attributes[i].location = attr.location;
        desc.attributes[i].format = attr.format;
        desc.attributes[i].offset = attr.offset;
        desc.attributes[i].semantic = intern_vertex_semantic(attr.semantic.c_str());
    }
    return desc;
}

VertexLayoutDesc make_vertex_layout_desc(const VertexLayoutDesc& layout) {
    VertexLayoutDesc desc;
    desc.stride = layout.stride;
    desc.per_instance = layout.per_instance;
    desc.use_shader_input_locations = layout.use_shader_input_locations;
    const uint32_t count = std::min<uint32_t>(
        layout.attribute_count,
        TGFX2_VERTEX_ATTRIBUTE_MAX);
    desc.attribute_count = count;
    if (layout.attribute_count > TGFX2_VERTEX_ATTRIBUTE_MAX) {
        tc::Log::error(
            "make_vertex_layout_desc: desc has %u attributes, max is %u; truncating",
            layout.attribute_count,
            TGFX2_VERTEX_ATTRIBUTE_MAX);
    }
    for (uint32_t i = 0; i < count; ++i) {
        desc.attributes[i] = layout.attributes[i];
        desc.attributes[i].semantic =
            intern_vertex_semantic(layout.attributes[i].semantic);
    }
    return desc;
}

VertexBufferLayout make_vertex_buffer_layout(const VertexLayoutDesc& desc) {
    VertexBufferLayout layout;
    layout.stride = desc.stride;
    layout.per_instance = desc.per_instance;
    layout.use_shader_input_locations = desc.use_shader_input_locations;
    const uint32_t count = std::min<uint32_t>(
        desc.attribute_count,
        TGFX2_VERTEX_ATTRIBUTE_MAX);
    layout.attributes.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const VertexAttributeDesc& attr = desc.attributes[i];
        layout.attributes.push_back({
            attr.location,
            attr.format,
            attr.offset,
            std::string(semantic_view(attr.semantic)),
        });
    }
    return layout;
}

bool vertex_layout_desc_equal(
    const VertexLayoutDesc& a,
    const VertexLayoutDesc& b
) {
    if (a.stride != b.stride ||
        a.per_instance != b.per_instance ||
        a.use_shader_input_locations != b.use_shader_input_locations ||
        a.attribute_count != b.attribute_count) {
        return false;
    }
    const uint32_t count = std::min<uint32_t>(
        a.attribute_count,
        TGFX2_VERTEX_ATTRIBUTE_MAX);
    for (uint32_t i = 0; i < count; ++i) {
        const VertexAttributeDesc& aa = a.attributes[i];
        const VertexAttributeDesc& bb = b.attributes[i];
        if (!a.use_shader_input_locations && aa.location != bb.location) {
            return false;
        }
        if (aa.format != bb.format ||
            aa.offset != bb.offset ||
            !semantic_equal(aa.semantic, bb.semantic)) {
            return false;
        }
    }
    return true;
}

size_t hash_vertex_layout_desc(const VertexLayoutDesc& layout) {
    size_t hash = 0;
    hash_combine(hash, std::hash<uint32_t>{}(layout.stride));
    hash_combine(hash, std::hash<bool>{}(layout.per_instance));
    hash_combine(hash, std::hash<bool>{}(layout.use_shader_input_locations));
    hash_combine(hash, std::hash<uint32_t>{}(layout.attribute_count));
    const uint32_t count = std::min<uint32_t>(
        layout.attribute_count,
        TGFX2_VERTEX_ATTRIBUTE_MAX);
    for (uint32_t i = 0; i < count; ++i) {
        const VertexAttributeDesc& attr = layout.attributes[i];
        if (!layout.use_shader_input_locations) {
            hash_combine(hash, std::hash<uint32_t>{}(attr.location));
        }
        hash_combine(hash, std::hash<int>{}(static_cast<int>(attr.format)));
        hash_combine(hash, std::hash<uint32_t>{}(attr.offset));
        hash_combine(hash, std::hash<std::string_view>{}(semantic_view(attr.semantic)));
    }
    return hash;
}

size_t hash_vertex_layout_descs(
    const VertexLayoutDesc* layouts,
    uint32_t count
) {
    size_t hash = 0;
    hash_combine(hash, std::hash<uint32_t>{}(count));
    for (uint32_t i = 0; i < count; ++i) {
        hash_combine(hash, hash_vertex_layout_desc(layouts[i]));
    }
    return hash;
}

} // namespace tgfx
