#include "tgfx2/d3d11/d3d11_render_device.hpp"

#include "tgfx2/d3d11/d3d11_command_list.hpp"
#include "tgfx2/d3d11/d3d11_type_conversions.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <tcbase/tc_log.hpp>

extern "C" {
#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_mesh_registry.h"
#include "tgfx/resources/tc_shader.h"
#include "tgfx/resources/tc_shader_registry.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
}

namespace {

void d3d11_invalidate_tc_shader_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::D3D11RenderDevice*>(user)->invalidate_tc_shader_cache(pool_index);
}

void d3d11_invalidate_tc_texture_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::D3D11RenderDevice*>(user)->invalidate_tc_texture_cache(pool_index);
}

void d3d11_invalidate_tc_mesh_trampoline(uint32_t pool_index, void* user) {
    static_cast<tgfx::D3D11RenderDevice*>(user)->invalidate_tc_mesh_cache(pool_index);
}

} // namespace

namespace tgfx {
namespace {

void throw_if_failed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%s failed: HRESULT=0x%08X", what, static_cast<unsigned>(hr));
        throw std::runtime_error(buffer);
    }
}

UINT buffer_bind_flags(BufferUsage usage) {
    UINT flags = 0;
    if (has_flag(usage, BufferUsage::Vertex)) flags |= D3D11_BIND_VERTEX_BUFFER;
    if (has_flag(usage, BufferUsage::Index)) flags |= D3D11_BIND_INDEX_BUFFER;
    if (has_flag(usage, BufferUsage::Uniform)) flags |= D3D11_BIND_CONSTANT_BUFFER;
    if (has_flag(usage, BufferUsage::Storage)) flags |= D3D11_BIND_SHADER_RESOURCE;
    return flags ? flags : D3D11_BIND_VERTEX_BUFFER;
}

UINT texture_bind_flags(TextureUsage usage, PixelFormat format) {
    UINT flags = 0;
    if (has_flag(usage, TextureUsage::Sampled)) flags |= D3D11_BIND_SHADER_RESOURCE;
    if (has_flag(usage, TextureUsage::ColorAttachment)) flags |= D3D11_BIND_RENDER_TARGET;
    if (has_flag(usage, TextureUsage::DepthStencilAttachment) || d3d11::is_depth_format(format)) {
        flags |= D3D11_BIND_DEPTH_STENCIL;
    }
    if (has_flag(usage, TextureUsage::Storage)) flags |= D3D11_BIND_UNORDERED_ACCESS;
    return flags ? flags : D3D11_BIND_SHADER_RESOURCE;
}

D3D11_USAGE buffer_usage(const BufferDesc& desc) {
    return desc.cpu_visible ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
}

UINT buffer_cpu_access(const BufferDesc& desc) {
    return desc.cpu_visible ? D3D11_CPU_ACCESS_WRITE : 0;
}

std::string semantic_name_for_attribute(const VertexAttribute& attr) {
    if (!attr.semantic.empty()) {
        return attr.semantic;
    }
    return "TEXCOORD";
}

PixelFormat tc_format_to_tgfx2(tc_texture_format fmt) {
    switch (fmt) {
        case TC_TEXTURE_RGBA8: return PixelFormat::RGBA8_UNorm;
        case TC_TEXTURE_RGB8: return PixelFormat::RGBA8_UNorm;
        case TC_TEXTURE_RG8: return PixelFormat::RG8_UNorm;
        case TC_TEXTURE_R8: return PixelFormat::R8_UNorm;
        case TC_TEXTURE_RGBA16F: return PixelFormat::RGBA16F;
        case TC_TEXTURE_RGB16F: return PixelFormat::RGBA16F;
        case TC_TEXTURE_DEPTH24: return PixelFormat::D24_UNorm_S8_UInt;
        case TC_TEXTURE_DEPTH32F: return PixelFormat::D32F;
        case TC_TEXTURE_R16F: return PixelFormat::R16F;
        case TC_TEXTURE_R32F: return PixelFormat::R32F;
    }
    return PixelFormat::RGBA8_UNorm;
}

TextureUsage tc_usage_to_tgfx(uint32_t usage) {
    uint32_t out = 0;
    if (usage & TC_TEXTURE_USAGE_SAMPLED)
        out |= static_cast<uint32_t>(TextureUsage::Sampled);
    if (usage & TC_TEXTURE_USAGE_COLOR_ATTACHMENT)
        out |= static_cast<uint32_t>(TextureUsage::ColorAttachment);
    if (usage & TC_TEXTURE_USAGE_DEPTH_ATTACHMENT)
        out |= static_cast<uint32_t>(TextureUsage::DepthStencilAttachment);
    if (usage & TC_TEXTURE_USAGE_COPY_SRC)
        out |= static_cast<uint32_t>(TextureUsage::CopySrc);
    if (usage & TC_TEXTURE_USAGE_COPY_DST)
        out |= static_cast<uint32_t>(TextureUsage::CopyDst);
    return static_cast<TextureUsage>(out);
}

std::vector<uint8_t> normalize_tc_texture_pixels(const tc_texture* tex, PixelFormat& out_format) {
    if (!tex || !tex->data) {
        return {};
    }

    const auto format = static_cast<tc_texture_format>(tex->format);
    const uint8_t* src = static_cast<const uint8_t*>(tex->data);
    const size_t pixel_count = static_cast<size_t>(tex->width) * static_cast<size_t>(tex->height);

    if (format == TC_TEXTURE_RGB8) {
        out_format = PixelFormat::RGBA8_UNorm;
        std::vector<uint8_t> pixels(pixel_count * 4u);
        for (size_t i = 0; i < pixel_count; ++i) {
            pixels[i * 4u + 0u] = src[i * 3u + 0u];
            pixels[i * 4u + 1u] = src[i * 3u + 1u];
            pixels[i * 4u + 2u] = src[i * 3u + 2u];
            pixels[i * 4u + 3u] = 0xffu;
        }
        return pixels;
    }

    if (format == TC_TEXTURE_RGB16F) {
        out_format = PixelFormat::RGBA16F;
        std::vector<uint8_t> pixels(pixel_count * 8u);
        for (size_t i = 0; i < pixel_count; ++i) {
            std::memcpy(&pixels[i * 8u], &src[i * 6u], 6u);
            pixels[i * 8u + 6u] = 0x00u;
            pixels[i * 8u + 7u] = 0x3cu; // IEEE-754 half 1.0, little endian.
        }
        return pixels;
    }

    out_format = tc_format_to_tgfx2(format);
    const size_t bytes = pixel_count * tc_texture_format_bpp(format);
    return std::vector<uint8_t>(src, src + bytes);
}

} // namespace

D3D11RenderDevice::D3D11RenderDevice() {
    create_device();
    query_capabilities();
    tc_shader_registry_add_destroy_hook(&d3d11_invalidate_tc_shader_trampoline, this);
    tc_texture_registry_add_destroy_hook(&d3d11_invalidate_tc_texture_trampoline, this);
    tc_mesh_registry_add_destroy_hook(&d3d11_invalidate_tc_mesh_trampoline, this);
}

D3D11RenderDevice::~D3D11RenderDevice() {
    tc_mesh_registry_remove_destroy_hook(&d3d11_invalidate_tc_mesh_trampoline, this);
    tc_texture_registry_remove_destroy_hook(&d3d11_invalidate_tc_texture_trampoline, this);
    tc_shader_registry_remove_destroy_hook(&d3d11_invalidate_tc_shader_trampoline, this);

    for (auto& pair : tc_mesh_cache_) {
        if (pair.second.vbo) destroy(pair.second.vbo);
        if (pair.second.ebo) destroy(pair.second.ebo);
    }
    tc_mesh_cache_.clear();
    for (auto& pair : tc_texture_cache_) {
        if (pair.second.handle) destroy(pair.second.handle);
    }
    tc_texture_cache_.clear();
    for (auto& pair : tc_shader_cache_) {
        if (pair.second.vs) destroy(pair.second.vs);
        if (pair.second.fs) destroy(pair.second.fs);
    }
    tc_shader_cache_.clear();
    wait_idle();
}

void D3D11RenderDevice::create_device() {
    std::array<D3D_FEATURE_LEVEL, 3> levels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
    };

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels.data(),
        static_cast<UINT>(levels.size()),
        D3D11_SDK_VERSION,
        &device_,
        &feature_level_,
        &context_);

#if defined(_DEBUG)
    if (FAILED(hr)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels.data(),
            static_cast<UINT>(levels.size()),
            D3D11_SDK_VERSION,
            &device_,
            &feature_level_,
            &context_);
    }
#endif

    throw_if_failed(hr, "D3D11CreateDevice");
}

void D3D11RenderDevice::query_capabilities() {
    caps_.backend = BackendType::D3D11;
    caps_.texture_origin_top_left = true;
    caps_.supports_compute = feature_level_ >= D3D_FEATURE_LEVEL_11_0;
    caps_.supports_geometry_shaders = true;
    caps_.supports_timestamp_queries = true;
    caps_.supports_multisample_resolve = true;
    caps_.max_color_attachments = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
    caps_.max_texture_dimension_2d = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    caps_.max_texture_units = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
}

void D3D11RenderDevice::wait_idle() {
    if (context_) {
        context_->Flush();
    }
}

BufferHandle D3D11RenderDevice::create_buffer(const BufferDesc& desc) {
    if (desc.size == 0) {
        tc::Log::error("D3D11RenderDevice::create_buffer: zero-sized buffers are not supported");
        return {};
    }

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = static_cast<UINT>(desc.size);
    bd.Usage = buffer_usage(desc);
    bd.BindFlags = buffer_bind_flags(desc.usage);
    bd.CPUAccessFlags = buffer_cpu_access(desc);
    bd.MiscFlags = 0;
    bd.StructureByteStride = 0;

    if (has_flag(desc.usage, BufferUsage::Uniform)) {
        bd.ByteWidth = (bd.ByteWidth + 15u) & ~15u;
    }

    D3D11Buffer out;
    out.desc = desc;
    HRESULT hr = device_->CreateBuffer(&bd, nullptr, &out.buffer);
    if (FAILED(hr)) {
        tc::Log::error("D3D11RenderDevice::create_buffer failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        return {};
    }
    return {buffers_.add(std::move(out))};
}

TextureHandle D3D11RenderDevice::create_texture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0) {
        tc::Log::error("D3D11RenderDevice::create_texture: zero-sized texture");
        return {};
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = desc.width;
    td.Height = desc.height;
    td.MipLevels = std::max(1u, desc.mip_levels);
    td.ArraySize = 1;
    td.Format = d3d11::to_dxgi_format(desc.format);
    td.SampleDesc.Count = std::max(1u, desc.sample_count);
    td.SampleDesc.Quality = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = texture_bind_flags(desc.usage, desc.format);

    D3D11Texture out;
    out.desc = desc;
    HRESULT hr = device_->CreateTexture2D(&td, nullptr, &out.texture);
    if (FAILED(hr)) {
        tc::Log::error("D3D11RenderDevice::create_texture failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        return {};
    }

    if ((td.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = d3d11::to_dxgi_srv_format(desc.format);
        sv.ViewDimension = td.SampleDesc.Count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        if (sv.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D) {
            sv.Texture2D.MipLevels = td.MipLevels;
        }
        hr = device_->CreateShaderResourceView(out.texture.Get(), &sv, &out.srv);
        if (FAILED(hr)) {
            tc::Log::error("D3D11RenderDevice::create_texture SRV failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        }
    }

    if ((td.BindFlags & D3D11_BIND_RENDER_TARGET) != 0) {
        hr = device_->CreateRenderTargetView(out.texture.Get(), nullptr, &out.rtv);
        if (FAILED(hr)) {
            tc::Log::error("D3D11RenderDevice::create_texture RTV failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        }
    }

    if ((td.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = d3d11::to_dxgi_dsv_format(desc.format);
        dv.ViewDimension = td.SampleDesc.Count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        hr = device_->CreateDepthStencilView(out.texture.Get(), &dv, &out.dsv);
        if (FAILED(hr)) {
            tc::Log::error("D3D11RenderDevice::create_texture DSV failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        }
    }

    return {textures_.add(std::move(out))};
}

SamplerHandle D3D11RenderDevice::create_sampler(const SamplerDesc& desc) {
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = d3d11::to_d3d_filter(desc);
    sd.AddressU = d3d11::to_d3d_address(desc.address_u);
    sd.AddressV = d3d11::to_d3d_address(desc.address_v);
    sd.AddressW = d3d11::to_d3d_address(desc.address_w);
    sd.MipLODBias = 0.0f;
    sd.MaxAnisotropy = static_cast<UINT>(std::max(1.0f, desc.max_anisotropy));
    sd.ComparisonFunc = d3d11::to_d3d_compare(desc.compare_op);
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = 0.0f;
    sd.BorderColor[3] = 1.0f;
    sd.MinLOD = 0.0f;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    D3D11Sampler out;
    HRESULT hr = device_->CreateSamplerState(&sd, &out.sampler);
    if (FAILED(hr)) {
        tc::Log::error("D3D11RenderDevice::create_sampler failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        return {};
    }
    return {samplers_.add(std::move(out))};
}

ShaderHandle D3D11RenderDevice::create_shader(const ShaderDesc& desc) {
    if (desc.bytecode.empty()) {
        tc::Log::error("D3D11RenderDevice::create_shader: D3D11 requires compiled bytecode");
        return {};
    }

    D3D11ShaderModule out;
    out.stage = desc.stage;
    out.bytecode = desc.bytecode;
    const void* bytes = out.bytecode.data();
    const SIZE_T size = out.bytecode.size();

    HRESULT hr = S_OK;
    switch (desc.stage) {
        case ShaderStage::Vertex:
            hr = device_->CreateVertexShader(bytes, size, nullptr, &out.vertex_shader);
            break;
        case ShaderStage::Fragment:
            hr = device_->CreatePixelShader(bytes, size, nullptr, &out.pixel_shader);
            break;
        case ShaderStage::Geometry:
            hr = device_->CreateGeometryShader(bytes, size, nullptr, &out.geometry_shader);
            break;
        case ShaderStage::Compute:
            hr = device_->CreateComputeShader(bytes, size, nullptr, &out.compute_shader);
            break;
    }
    if (FAILED(hr)) {
        tc::Log::error("D3D11RenderDevice::create_shader failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        return {};
    }
    return {shaders_.add(std::move(out))};
}

PipelineHandle D3D11RenderDevice::create_pipeline(const PipelineDesc& desc) {
    auto* vs = get_shader(desc.vertex_shader);
    auto* fs = get_shader(desc.fragment_shader);
    if (!vs || !vs->vertex_shader || !fs || !fs->pixel_shader) {
        throw std::runtime_error("D3D11 pipeline requires valid vertex and fragment shader bytecode");
    }

    D3D11Pipeline out;
    out.desc = desc;

    std::vector<std::string> semantic_names;
    std::vector<D3D11_INPUT_ELEMENT_DESC> input_elements;
    size_t input_element_count = 0;
    for (const auto& layout : desc.vertex_layouts) {
        input_element_count += layout.attributes.size();
    }
    semantic_names.reserve(input_element_count);
    input_elements.reserve(input_element_count);
    for (uint32_t slot = 0; slot < desc.vertex_layouts.size(); ++slot) {
        const auto& layout = desc.vertex_layouts[slot];
        for (const auto& attr : layout.attributes) {
            semantic_names.push_back(semantic_name_for_attribute(attr));
            D3D11_INPUT_ELEMENT_DESC element{};
            element.SemanticName = semantic_names.back().c_str();
            element.SemanticIndex = attr.semantic.empty() ? attr.location : 0;
            element.Format = d3d11::to_dxgi_vertex_format(attr.format);
            element.InputSlot = slot;
            element.AlignedByteOffset = attr.offset;
            element.InputSlotClass = layout.per_instance
                ? D3D11_INPUT_PER_INSTANCE_DATA
                : D3D11_INPUT_PER_VERTEX_DATA;
            element.InstanceDataStepRate = layout.per_instance ? 1u : 0u;
            input_elements.push_back(element);
        }
    }
    if (!input_elements.empty()) {
        throw_if_failed(
            device_->CreateInputLayout(
                input_elements.data(),
                static_cast<UINT>(input_elements.size()),
                vs->bytecode.data(),
                vs->bytecode.size(),
                &out.input_layout),
            "ID3D11Device::CreateInputLayout");
    }

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = d3d11::to_d3d_fill(desc.raster.polygon_mode);
    rd.CullMode = d3d11::to_d3d_cull(desc.raster.cull);
    rd.FrontCounterClockwise = desc.raster.front_face == FrontFace::CCW;
    rd.DepthBias = static_cast<INT>(desc.raster.depth_bias_constant);
    rd.DepthBiasClamp = desc.raster.depth_bias_clamp;
    rd.SlopeScaledDepthBias = desc.raster.depth_bias_slope;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = TRUE;
    rd.MultisampleEnable = desc.sample_count > 1;
    throw_if_failed(device_->CreateRasterizerState(&rd, &out.raster_state),
                    "ID3D11Device::CreateRasterizerState");

    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = desc.depth_stencil.depth_test;
    dsd.DepthWriteMask = desc.depth_stencil.depth_write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dsd.DepthFunc = d3d11::to_d3d_compare(desc.depth_stencil.depth_compare);
    dsd.StencilEnable = FALSE;
    throw_if_failed(device_->CreateDepthStencilState(&dsd, &out.depth_stencil_state),
                    "ID3D11Device::CreateDepthStencilState");

    D3D11_BLEND_DESC bd{};
    bd.AlphaToCoverageEnable = FALSE;
    bd.IndependentBlendEnable = FALSE;
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = desc.blend.enabled;
    rt.SrcBlend = d3d11::to_d3d_blend_factor(desc.blend.src_color);
    rt.DestBlend = d3d11::to_d3d_blend_factor(desc.blend.dst_color);
    rt.BlendOp = d3d11::to_d3d_blend_op(desc.blend.color_op);
    rt.SrcBlendAlpha = d3d11::to_d3d_blend_factor(desc.blend.src_alpha);
    rt.DestBlendAlpha = d3d11::to_d3d_blend_factor(desc.blend.dst_alpha);
    rt.BlendOpAlpha = d3d11::to_d3d_blend_op(desc.blend.alpha_op);
    rt.RenderTargetWriteMask =
        (desc.color_mask.r ? D3D11_COLOR_WRITE_ENABLE_RED : 0) |
        (desc.color_mask.g ? D3D11_COLOR_WRITE_ENABLE_GREEN : 0) |
        (desc.color_mask.b ? D3D11_COLOR_WRITE_ENABLE_BLUE : 0) |
        (desc.color_mask.a ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0);
    throw_if_failed(device_->CreateBlendState(&bd, &out.blend_state),
                    "ID3D11Device::CreateBlendState");

    return {pipelines_.add(std::move(out))};
}

ResourceSetHandle D3D11RenderDevice::create_resource_set(const ResourceSetDesc& desc) {
    D3D11ResourceSet out;
    out.desc = desc;
    return {resource_sets_.add(std::move(out))};
}

uintptr_t D3D11RenderDevice::pipeline_descriptor_set_layout(PipelineHandle pipeline) const {
    return pipelines_.get_const(pipeline.id) ? static_cast<uintptr_t>(pipeline.id) : 0;
}

void D3D11RenderDevice::destroy(BufferHandle handle) { buffers_.remove(handle.id); }
void D3D11RenderDevice::destroy(TextureHandle handle) { textures_.remove(handle.id); }
void D3D11RenderDevice::destroy(SamplerHandle handle) { samplers_.remove(handle.id); }
void D3D11RenderDevice::destroy(ShaderHandle handle) { shaders_.remove(handle.id); }
void D3D11RenderDevice::destroy(PipelineHandle handle) { pipelines_.remove(handle.id); }
void D3D11RenderDevice::destroy(ResourceSetHandle handle) { resource_sets_.remove(handle.id); }

void D3D11RenderDevice::upload_buffer(BufferHandle dst, std::span<const uint8_t> data, uint64_t offset) {
    auto* buf = get_buffer(dst);
    if (!buf || !buf->buffer || data.empty()) return;
    if (buf->desc.cpu_visible) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(buf->buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) {
            tc::Log::error("D3D11RenderDevice::upload_buffer Map failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
            return;
        }
        std::memcpy(static_cast<uint8_t*>(mapped.pData) + offset, data.data(), data.size());
        context_->Unmap(buf->buffer.Get(), 0);
        return;
    }
    D3D11_BOX box{};
    box.left = static_cast<UINT>(offset);
    box.right = static_cast<UINT>(offset + data.size());
    box.bottom = 1;
    box.back = 1;
    context_->UpdateSubresource(buf->buffer.Get(), 0, &box, data.data(), 0, 0);
}

void D3D11RenderDevice::upload_texture(TextureHandle dst, std::span<const uint8_t> data, uint32_t mip) {
    auto* tex = get_texture(dst);
    if (!tex || !tex->texture || data.empty()) return;
    const uint32_t w = std::max(1u, tex->desc.width >> mip);
    const uint32_t row_pitch = w * d3d11::pixel_format_bytes(tex->desc.format);
    context_->UpdateSubresource(tex->texture.Get(), mip, nullptr, data.data(), row_pitch, 0);
}

void D3D11RenderDevice::upload_texture_region(TextureHandle dst,
                                              uint32_t x, uint32_t y,
                                              uint32_t w, uint32_t h,
                                              std::span<const uint8_t> data,
                                              uint32_t mip) {
    auto* tex = get_texture(dst);
    if (!tex || !tex->texture || data.empty()) return;
    D3D11_BOX box{};
    box.left = x;
    box.top = y;
    box.right = x + w;
    box.bottom = y + h;
    box.front = 0;
    box.back = 1;
    const uint32_t row_pitch = w * d3d11::pixel_format_bytes(tex->desc.format);
    context_->UpdateSubresource(tex->texture.Get(), mip, &box, data.data(), row_pitch, 0);
}

void D3D11RenderDevice::read_buffer(BufferHandle src, std::span<uint8_t> data, uint64_t offset) {
    auto* buf = get_buffer(src);
    if (!buf || !buf->buffer || data.empty()) return;

    D3D11_BUFFER_DESC src_desc{};
    buf->buffer->GetDesc(&src_desc);
    D3D11_BUFFER_DESC staging_desc = src_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
    HRESULT hr = device_->CreateBuffer(&staging_desc, nullptr, &staging);
    if (FAILED(hr)) {
        tc::Log::error("D3D11RenderDevice::read_buffer staging allocation failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        return;
    }
    context_->CopyResource(staging.Get(), buf->buffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        tc::Log::error("D3D11RenderDevice::read_buffer Map failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        return;
    }
    std::memcpy(data.data(), static_cast<const uint8_t*>(mapped.pData) + offset, data.size());
    context_->Unmap(staging.Get(), 0);
}

TextureDesc D3D11RenderDevice::texture_desc(TextureHandle handle) const {
    auto* tex = textures_.get_const(handle.id);
    return tex ? tex->desc : TextureDesc{};
}

std::unique_ptr<ICommandList> D3D11RenderDevice::create_command_list(QueueType queue) {
    if (queue != QueueType::Graphics) {
        throw std::runtime_error("D3D11RenderDevice: only graphics command lists are implemented");
    }
    return std::make_unique<D3D11CommandList>(*this);
}

void D3D11RenderDevice::submit(ICommandList& /*cmd*/) {
    context_->Flush();
}

void D3D11RenderDevice::present() {
    context_->Flush();
}

TextureHandle D3D11RenderDevice::register_external_texture(uintptr_t native_handle, const TextureDesc& desc) {
    return register_external_texture(reinterpret_cast<ID3D11Texture2D*>(native_handle), desc);
}

TextureHandle D3D11RenderDevice::register_external_texture(ID3D11Texture2D* texture, const TextureDesc& desc) {
    if (!texture) {
        tc::Log::error("D3D11RenderDevice::register_external_texture: null native texture");
        return {};
    }

    D3D11Texture out;
    out.texture = texture;
    out.desc = desc;

    if (has_flag(desc.usage, TextureUsage::Sampled)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = d3d11::to_dxgi_srv_format(desc.format);
        sv.ViewDimension = desc.sample_count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        if (sv.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D) {
            sv.Texture2D.MipLevels = std::max(1u, desc.mip_levels);
        }
        HRESULT hr = device_->CreateShaderResourceView(out.texture.Get(), &sv, &out.srv);
        if (FAILED(hr)) {
            tc::Log::error(
                "D3D11RenderDevice::register_external_texture SRV failed: HRESULT=0x%08X",
                static_cast<unsigned>(hr));
        }
    }

    if (has_flag(desc.usage, TextureUsage::ColorAttachment)) {
        HRESULT hr = device_->CreateRenderTargetView(out.texture.Get(), nullptr, &out.rtv);
        if (FAILED(hr)) {
            tc::Log::error(
                "D3D11RenderDevice::register_external_texture RTV failed: HRESULT=0x%08X",
                static_cast<unsigned>(hr));
        }
    }

    if (has_flag(desc.usage, TextureUsage::DepthStencilAttachment) || d3d11::is_depth_format(desc.format)) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = d3d11::to_dxgi_dsv_format(desc.format);
        dv.ViewDimension = desc.sample_count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        HRESULT hr = device_->CreateDepthStencilView(out.texture.Get(), &dv, &out.dsv);
        if (FAILED(hr)) {
            tc::Log::error(
                "D3D11RenderDevice::register_external_texture DSV failed: HRESULT=0x%08X",
                static_cast<unsigned>(hr));
        }
    }

    return {textures_.add(std::move(out))};
}

void D3D11RenderDevice::blit_to_texture(TextureHandle dst,
                                        TextureHandle src,
                                        int src_x,
                                        int src_y,
                                        int src_w,
                                        int src_h,
                                        int dst_x,
                                        int dst_y,
                                        int dst_w,
                                        int dst_h) {
    auto* src_tex = get_texture(src);
    auto* dst_tex = get_texture(dst);
    if (!src_tex || !src_tex->texture || !dst_tex || !dst_tex->texture) {
        tc::Log::error(
            "D3D11RenderDevice::blit_to_texture: invalid texture handle src=%u dst=%u",
            src.id,
            dst.id);
        return;
    }

    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        tc::Log::error("D3D11RenderDevice::blit_to_texture: invalid empty region");
        return;
    }
    if (src_w != dst_w || src_h != dst_h) {
        tc::Log::error(
            "D3D11RenderDevice::blit_to_texture: scaled blits are not implemented "
            "(src=%dx%d dst=%dx%d)",
            src_w,
            src_h,
            dst_w,
            dst_h);
        return;
    }
    if (src_x < 0 || src_y < 0 || dst_x < 0 || dst_y < 0 ||
        src_x + src_w > static_cast<int>(src_tex->desc.width) ||
        src_y + src_h > static_cast<int>(src_tex->desc.height) ||
        dst_x + dst_w > static_cast<int>(dst_tex->desc.width) ||
        dst_y + dst_h > static_cast<int>(dst_tex->desc.height)) {
        tc::Log::error("D3D11RenderDevice::blit_to_texture: region outside texture bounds");
        return;
    }

    D3D11_BOX src_box{};
    src_box.left = static_cast<UINT>(src_x);
    src_box.top = static_cast<UINT>(src_y);
    src_box.front = 0;
    src_box.right = static_cast<UINT>(src_x + src_w);
    src_box.bottom = static_cast<UINT>(src_y + src_h);
    src_box.back = 1;
    context_->CopySubresourceRegion(
        dst_tex->texture.Get(),
        0,
        static_cast<UINT>(dst_x),
        static_cast<UINT>(dst_y),
        0,
        src_tex->texture.Get(),
        0,
        &src_box);
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> D3D11RenderDevice::create_staging_texture(const D3D11Texture& src) const {
    D3D11_TEXTURE2D_DESC desc{};
    src.texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) {
        tc::Log::error("D3D11RenderDevice::create_staging_texture failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
    }
    return staging;
}

bool D3D11RenderDevice::read_pixel_rgba8(TextureHandle handle, int x, int y, float out_rgba[4]) {
    if (!out_rgba) return false;
    auto* tex = get_texture(handle);
    if (!tex || !tex->texture) return false;
    if (tex->desc.format != PixelFormat::RGBA8_UNorm && tex->desc.format != PixelFormat::BGRA8_UNorm) {
        tc::Log::error("D3D11RenderDevice::read_pixel_rgba8: unsupported format");
        return false;
    }
    if (x < 0 || y < 0 || x >= static_cast<int>(tex->desc.width) || y >= static_cast<int>(tex->desc.height)) {
        return false;
    }

    auto staging = create_staging_texture(*tex);
    if (!staging) return false;
    context_->CopyResource(staging.Get(), tex->texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        tc::Log::error("D3D11RenderDevice::read_pixel_rgba8 Map failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        return false;
    }
    const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
    const auto* p = row + static_cast<size_t>(x) * 4u;
    if (tex->desc.format == PixelFormat::BGRA8_UNorm) {
        out_rgba[0] = p[2] / 255.0f;
        out_rgba[1] = p[1] / 255.0f;
        out_rgba[2] = p[0] / 255.0f;
        out_rgba[3] = p[3] / 255.0f;
    } else {
        out_rgba[0] = p[0] / 255.0f;
        out_rgba[1] = p[1] / 255.0f;
        out_rgba[2] = p[2] / 255.0f;
        out_rgba[3] = p[3] / 255.0f;
    }
    context_->Unmap(staging.Get(), 0);
    return true;
}

bool D3D11RenderDevice::read_texture_rgba_float(TextureHandle handle, float* out) {
    auto* tex = get_texture(handle);
    if (!tex || !tex->texture || !out) return false;
    if (tex->desc.format != PixelFormat::RGBA8_UNorm && tex->desc.format != PixelFormat::BGRA8_UNorm) {
        tc::Log::error("D3D11RenderDevice::read_texture_rgba_float: unsupported format");
        return false;
    }
    auto staging = create_staging_texture(*tex);
    if (!staging) return false;
    context_->CopyResource(staging.Get(), tex->texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        tc::Log::error("D3D11RenderDevice::read_texture_rgba_float Map failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
        return false;
    }
    for (uint32_t y = 0; y < tex->desc.height; ++y) {
        const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        for (uint32_t x = 0; x < tex->desc.width; ++x) {
            const auto* p = row + static_cast<size_t>(x) * 4u;
            float* dst = out + (static_cast<size_t>(y) * tex->desc.width + x) * 4u;
            if (tex->desc.format == PixelFormat::BGRA8_UNorm) {
                dst[0] = p[2] / 255.0f;
                dst[1] = p[1] / 255.0f;
                dst[2] = p[0] / 255.0f;
                dst[3] = p[3] / 255.0f;
            } else {
                dst[0] = p[0] / 255.0f;
                dst[1] = p[1] / 255.0f;
                dst[2] = p[2] / 255.0f;
                dst[3] = p[3] / 255.0f;
            }
        }
    }
    context_->Unmap(staging.Get(), 0);
    return true;
}

void D3D11RenderDevice::reset_state() {
    if (context_) {
        context_->ClearState();
    }
}

void D3D11RenderDevice::flush() {
    if (context_) context_->Flush();
}

void D3D11RenderDevice::finish() {
    wait_idle();
}

bool D3D11RenderDevice::ensure_tc_shader(
    tc_shader* shader,
    ShaderHandle* out_vs,
    ShaderHandle* out_fs)
{
    if (!shader) {
        tc::Log::error("D3D11RenderDevice::ensure_tc_shader: shader is NULL");
        return false;
    }
    if (!out_fs) {
        tc::Log::error("D3D11RenderDevice::ensure_tc_shader: out_fs is NULL");
        return false;
    }
    if (!shader->fragment_source) {
        tc::Log::error(
            "D3D11RenderDevice::ensure_tc_shader: missing fragment_source for '%s'",
            shader->name ? shader->name : shader->uuid);
        return false;
    }

    const bool has_vs = shader->vertex_source && shader->vertex_source[0] != '\0';
    const uint32_t pool_index = shader->pool_index;
    const uint32_t version = shader->version;
    const bool resource_layout_ready = tc_shader_has_resource_layout(shader) ||
                                       !tc_shader_requires_artifacts(shader);

    auto it = tc_shader_cache_.find(pool_index);
    if (it != tc_shader_cache_.end() &&
        it->second.version == version &&
        it->second.has_vs == has_vs &&
        it->second.fs &&
        (!has_vs || it->second.vs) &&
        resource_layout_ready) {
        if (out_vs) *out_vs = it->second.vs;
        *out_fs = it->second.fs;
        return true;
    }
    if (it != tc_shader_cache_.end()) {
        if (it->second.vs) destroy(it->second.vs);
        if (it->second.fs) destroy(it->second.fs);
        tc_shader_cache_.erase(it);
    }

    ShaderHandle vs;
    if (has_vs) {
        ShaderDesc vs_desc;
        vs_desc.stage = ShaderStage::Vertex;
        vs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":vertex";
        if (shader->vertex_entry && shader->vertex_entry[0]) {
            vs_desc.entry_point = shader->vertex_entry;
        }
        if (!termin::tgfx2_load_or_compile_shader_artifact_for_backend(
                shader,
                BackendType::D3D11,
                vs_desc.stage,
                vs_desc.bytecode)) {
            tc::Log::error(
                "D3D11RenderDevice::ensure_tc_shader: vertex .cso artifact missing or dev compile failed for '%s'",
                shader->name ? shader->name : shader->uuid);
            return false;
        }
        vs = create_shader(vs_desc);
        if (!vs) {
            tc::Log::error(
                "D3D11RenderDevice::ensure_tc_shader: VS create failed for '%s'",
                shader->name ? shader->name : shader->uuid);
            return false;
        }
    }

    ShaderDesc fs_desc;
    fs_desc.stage = ShaderStage::Fragment;
    fs_desc.debug_name = std::string(shader->name ? shader->name : shader->uuid) + ":fragment";
    if (shader->fragment_entry && shader->fragment_entry[0]) {
        fs_desc.entry_point = shader->fragment_entry;
    }
    if (!termin::tgfx2_load_or_compile_shader_artifact_for_backend(
            shader,
            BackendType::D3D11,
            fs_desc.stage,
            fs_desc.bytecode)) {
        if (vs) destroy(vs);
        tc::Log::error(
            "D3D11RenderDevice::ensure_tc_shader: fragment .cso artifact missing or dev compile failed for '%s'",
            shader->name ? shader->name : shader->uuid);
        return false;
    }
    ShaderHandle fs = create_shader(fs_desc);
    if (!fs) {
        if (vs) destroy(vs);
        tc::Log::error(
            "D3D11RenderDevice::ensure_tc_shader: PS create failed for '%s'",
            shader->name ? shader->name : shader->uuid);
        return false;
    }

    CachedTcShaderEntry entry;
    entry.vs = vs;
    entry.fs = fs;
    entry.version = version;
    entry.has_vs = has_vs;
    tc_shader_cache_.emplace(pool_index, entry);

    if (out_vs) *out_vs = vs;
    *out_fs = fs;
    return true;
}

void D3D11RenderDevice::invalidate_tc_shader_cache(uint32_t pool_index) {
    auto it = tc_shader_cache_.find(pool_index);
    if (it == tc_shader_cache_.end()) return;
    if (it->second.vs) destroy(it->second.vs);
    if (it->second.fs) destroy(it->second.fs);
    tc_shader_cache_.erase(it);
}

TextureHandle D3D11RenderDevice::ensure_tc_texture(tc_texture* tex) {
    if (!tex) return {};

    if (!tex->header.is_loaded && tex->header.load_callback) {
        tc_texture_ensure_loaded_ptr(tex);
    }

    const bool gpu_first = tex->storage_kind == TC_TEXTURE_STORAGE_GPU_FIRST;
    if (tex->width == 0 || tex->height == 0) {
        tc::Log::error(
            "D3D11RenderDevice::ensure_tc_texture: tc_texture '%s' has zero size",
            tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }
    if (!gpu_first && !tex->data) {
        tc::Log::error(
            "D3D11RenderDevice::ensure_tc_texture: tc_texture '%s' has no CPU pixels",
            tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }

    const uint32_t pool_index = tex->header.pool_index;
    const uint32_t version = tex->header.version;
    auto it = tc_texture_cache_.find(pool_index);
    if (it != tc_texture_cache_.end() && it->second.version == version) {
        return it->second.handle;
    }
    if (it != tc_texture_cache_.end()) {
        if (it->second.handle) destroy(it->second.handle);
        tc_texture_cache_.erase(it);
    }

    TextureDesc desc;
    desc.width = tex->width;
    desc.height = tex->height;
    desc.mip_levels = 1;
    desc.sample_count = 1;

    if (gpu_first) {
        desc.format = tc_format_to_tgfx2(static_cast<tc_texture_format>(tex->format));
        desc.usage = tc_usage_to_tgfx(tex->usage) | TextureUsage::CopyDst;
        if (static_cast<uint32_t>(desc.usage) == static_cast<uint32_t>(TextureUsage::CopyDst)) {
            desc.usage = desc.usage | TextureUsage::Sampled;
        }

        TextureHandle handle = create_texture(desc);
        if (!handle) {
            tc::Log::error(
                "D3D11RenderDevice::ensure_tc_texture: GPU-first create_texture failed for '%s'",
                tex->header.name ? tex->header.name : tex->header.uuid);
            return {};
        }
        tc_texture_cache_.emplace(pool_index, CachedTcTextureEntry{handle, version});
        return handle;
    }

    PixelFormat upload_format = PixelFormat::RGBA8_UNorm;
    std::vector<uint8_t> pixels = normalize_tc_texture_pixels(tex, upload_format);
    if (pixels.empty()) {
        return {};
    }

    desc.format = upload_format;
    desc.usage = TextureUsage::Sampled | TextureUsage::CopySrc | TextureUsage::CopyDst;
    TextureHandle handle = create_texture(desc);
    if (!handle) {
        tc::Log::error(
            "D3D11RenderDevice::ensure_tc_texture: create_texture failed for '%s'",
            tex->header.name ? tex->header.name : tex->header.uuid);
        return {};
    }

    upload_texture(handle, std::span<const uint8_t>(pixels.data(), pixels.size()));
    tc_texture_cache_.emplace(pool_index, CachedTcTextureEntry{handle, version});
    return handle;
}

void D3D11RenderDevice::invalidate_tc_texture_cache(uint32_t pool_index) {
    auto it = tc_texture_cache_.find(pool_index);
    if (it == tc_texture_cache_.end()) return;
    if (it->second.handle) destroy(it->second.handle);
    tc_texture_cache_.erase(it);
}

std::pair<BufferHandle, BufferHandle> D3D11RenderDevice::ensure_tc_mesh(tc_mesh* mesh) {
    if (!mesh) return {};

    if (!mesh->vertices || mesh->vertex_count == 0 ||
        !mesh->indices || mesh->index_count == 0 ||
        mesh->layout.stride == 0) {
        tc::Log::error(
            "D3D11RenderDevice::ensure_tc_mesh: tc_mesh '%s' has no CPU mesh data",
            mesh->header.name ? mesh->header.name : mesh->header.uuid);
        return {};
    }

    const uint32_t pool_index = mesh->header.pool_index;
    const uint32_t version = mesh->header.version;
    auto it = tc_mesh_cache_.find(pool_index);
    if (it != tc_mesh_cache_.end() && it->second.version == version) {
        return {it->second.vbo, it->second.ebo};
    }
    if (it != tc_mesh_cache_.end()) {
        if (it->second.vbo) destroy(it->second.vbo);
        if (it->second.ebo) destroy(it->second.ebo);
        tc_mesh_cache_.erase(it);
    }

    const size_t vb_size = tc_mesh_vertices_size(mesh);
    const size_t ib_size = tc_mesh_indices_size(mesh);

    BufferDesc vb_desc;
    vb_desc.size = vb_size;
    vb_desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    BufferHandle vbo = create_buffer(vb_desc);
    if (!vbo) {
        tc::Log::error(
            "D3D11RenderDevice::ensure_tc_mesh: failed to allocate VBO for '%s'",
            mesh->header.name ? mesh->header.name : mesh->header.uuid);
        return {};
    }
    upload_buffer(
        vbo,
        std::span<const uint8_t>(
            static_cast<const uint8_t*>(mesh->vertices), vb_size));

    BufferDesc ib_desc;
    ib_desc.size = ib_size;
    ib_desc.usage = BufferUsage::Index | BufferUsage::CopyDst;
    BufferHandle ebo = create_buffer(ib_desc);
    if (!ebo) {
        tc::Log::error(
            "D3D11RenderDevice::ensure_tc_mesh: failed to allocate EBO for '%s'",
            mesh->header.name ? mesh->header.name : mesh->header.uuid);
        destroy(vbo);
        return {};
    }
    upload_buffer(
        ebo,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(mesh->indices), ib_size));

    tc_mesh_cache_.emplace(pool_index, CachedTcMeshEntry{vbo, ebo, version});
    return {vbo, ebo};
}

void D3D11RenderDevice::invalidate_tc_mesh_cache(uint32_t pool_index) {
    auto it = tc_mesh_cache_.find(pool_index);
    if (it == tc_mesh_cache_.end()) return;
    if (it->second.vbo) destroy(it->second.vbo);
    if (it->second.ebo) destroy(it->second.ebo);
    tc_mesh_cache_.erase(it);
}

} // namespace tgfx
