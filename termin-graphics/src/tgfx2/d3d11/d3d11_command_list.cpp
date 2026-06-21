#include "tgfx2/d3d11/d3d11_command_list.hpp"

#include "tgfx2/d3d11/d3d11_type_conversions.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <tcbase/tc_log.hpp>
extern "C" {
#include "tgfx/resources/tc_shader.h"
}

namespace tgfx {

namespace {

uint32_t effective_stage_mask(const ResourceBinding& binding) {
    return binding.stage_mask == TC_SHADER_STAGE_NONE
        ? TC_SHADER_STAGE_ALL_GRAPHICS
        : binding.stage_mask;
}

UINT d3d11_slot(const ResourceBinding& binding) {
    const uint32_t base = binding.d3d11.has_placement
        ? binding.d3d11.register_index
        : binding.binding;
    return static_cast<UINT>(base + binding.array_element);
}

bool validate_d3d11_register_class(
    const ResourceBinding& binding,
    uint32_t expected,
    const char* kind_name
) {
    if (!binding.d3d11.has_placement) {
        return true;
    }
    if (binding.d3d11.register_class == expected) {
        return true;
    }
    tc::Log::error(
        "D3D11CommandList::bind_resource_set: %s binding at set=%u binding=%u "
        "has D3D11 class=%u, expected class=%u",
        kind_name,
        binding.set,
        binding.binding,
        binding.d3d11.register_class,
        expected);
    return false;
}

bool validate_d3d11_slot(
    const ResourceBinding& binding,
    UINT slot,
    UINT limit,
    const char* kind_name
) {
    if (slot < limit) {
        return true;
    }
    tc::Log::error(
        "D3D11CommandList::bind_resource_set: %s binding at set=%u binding=%u "
        "resolved to out-of-range D3D11 slot=%u limit=%u has_placement=%u",
        kind_name,
        binding.set,
        binding.binding,
        slot,
        limit,
        binding.d3d11.has_placement ? 1u : 0u);
    return false;
}

uint32_t effective_stage_mask(const BoundResourceBinding& binding) {
    return binding.plan_entry.stage_mask == TC_SHADER_STAGE_NONE
        ? TC_SHADER_STAGE_ALL_GRAPHICS
        : binding.plan_entry.stage_mask;
}

UINT d3d11_slot(const BoundResourceBinding& binding) {
    return static_cast<UINT>(
        binding.plan_entry.placement.d3d11.register_index +
        binding.value.array_element);
}

bool validate_d3d11_placement(
    const BoundResourceBinding& binding,
    D3D11RegisterClass expected,
    const char* kind_name
) {
    const BackendBindingPlanEntry& entry = binding.plan_entry;
    if (entry.placement.kind != BackendPlacementKind::D3D11Register) {
        tc::Log::error(
            "D3D11CommandList::bind_resource_set: %s resource '%s' has "
            "non-D3D11 placement kind=%u",
            kind_name,
            entry.resource.name.c_str(),
            static_cast<unsigned>(entry.placement.kind));
        return false;
    }
    if (entry.placement.d3d11.register_class == expected) {
        return true;
    }
    tc::Log::error(
        "D3D11CommandList::bind_resource_set: %s resource '%s' has "
        "D3D11 class=%u, expected class=%u",
        kind_name,
        entry.resource.name.c_str(),
        static_cast<unsigned>(entry.placement.d3d11.register_class),
        static_cast<unsigned>(expected));
    return false;
}

bool validate_d3d11_slot(
    const BoundResourceBinding& binding,
    UINT slot,
    UINT limit,
    const char* kind_name
) {
    if (slot < limit) {
        return true;
    }
    tc::Log::error(
        "D3D11CommandList::bind_resource_set: %s resource '%s' resolved to "
        "out-of-range D3D11 slot=%u limit=%u",
        kind_name,
        binding.plan_entry.resource.name.c_str(),
        slot,
        limit);
    return false;
}

void set_constant_buffers(
    ID3D11DeviceContext* ctx,
    uint32_t stage_mask,
    UINT slot,
    ID3D11Buffer* const* buffer
) {
    if (stage_mask & TC_SHADER_STAGE_VERTEX) {
        ctx->VSSetConstantBuffers(slot, 1, buffer);
    }
    if (stage_mask & TC_SHADER_STAGE_FRAGMENT) {
        ctx->PSSetConstantBuffers(slot, 1, buffer);
    }
    if (stage_mask & TC_SHADER_STAGE_GEOMETRY) {
        ctx->GSSetConstantBuffers(slot, 1, buffer);
    }
}

void set_shader_resources(
    ID3D11DeviceContext* ctx,
    uint32_t stage_mask,
    UINT slot,
    ID3D11ShaderResourceView* const* srv
) {
    if (stage_mask & TC_SHADER_STAGE_VERTEX) {
        ctx->VSSetShaderResources(slot, 1, srv);
    }
    if (stage_mask & TC_SHADER_STAGE_FRAGMENT) {
        ctx->PSSetShaderResources(slot, 1, srv);
    }
    if (stage_mask & TC_SHADER_STAGE_GEOMETRY) {
        ctx->GSSetShaderResources(slot, 1, srv);
    }
}

void set_samplers(
    ID3D11DeviceContext* ctx,
    uint32_t stage_mask,
    UINT slot,
    ID3D11SamplerState* const* sampler
) {
    if (stage_mask & TC_SHADER_STAGE_VERTEX) {
        ctx->VSSetSamplers(slot, 1, sampler);
    }
    if (stage_mask & TC_SHADER_STAGE_FRAGMENT) {
        ctx->PSSetSamplers(slot, 1, sampler);
    }
    if (stage_mask & TC_SHADER_STAGE_GEOMETRY) {
        ctx->GSSetSamplers(slot, 1, sampler);
    }
}

void clear_shader_resources(ID3D11DeviceContext* ctx) {
    std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> null_srvs{};
    ctx->VSSetShaderResources(0, static_cast<UINT>(null_srvs.size()), null_srvs.data());
    ctx->PSSetShaderResources(0, static_cast<UINT>(null_srvs.size()), null_srvs.data());
    ctx->GSSetShaderResources(0, static_cast<UINT>(null_srvs.size()), null_srvs.data());
}

void bind_legacy_resource_binding(
    D3D11RenderDevice& device,
    ID3D11DeviceContext* ctx,
    const ResourceBinding& binding
) {
    const UINT slot = d3d11_slot(binding);
    const uint32_t stage_mask = effective_stage_mask(binding);
    switch (binding.kind) {
        case ResourceBinding::Kind::UniformBuffer: {
            if (!validate_d3d11_register_class(
                    binding,
                    TC_SHADER_D3D11_REGISTER_B,
                    "uniform buffer")) {
                break;
            }
            if (!validate_d3d11_slot(
                    binding,
                    slot,
                    D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                    "uniform buffer")) {
                break;
            }
            auto* buf = device.get_buffer(binding.buffer);
            ID3D11Buffer* native = buf ? buf->buffer.Get() : nullptr;
            set_constant_buffers(ctx, stage_mask, slot, &native);
            break;
        }
        case ResourceBinding::Kind::SampledTexture: {
            if (!validate_d3d11_register_class(
                    binding,
                    TC_SHADER_D3D11_REGISTER_T,
                    "sampled texture")) {
                break;
            }
            if (!validate_d3d11_slot(
                    binding,
                    slot,
                    D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,
                    "sampled texture")) {
                break;
            }
            auto* tex = device.get_texture(binding.texture);
            ID3D11ShaderResourceView* srv = tex ? tex->srv.Get() : nullptr;
            set_shader_resources(ctx, stage_mask, slot, &srv);
            auto* sampler = device.get_sampler(binding.sampler);
            ID3D11SamplerState* native_sampler = sampler
                ? sampler->sampler.Get()
                : device.default_sampler_state();
            set_samplers(ctx, stage_mask, slot, &native_sampler);
            break;
        }
        case ResourceBinding::Kind::Sampler: {
            if (!validate_d3d11_register_class(
                    binding,
                    TC_SHADER_D3D11_REGISTER_S,
                    "sampler")) {
                break;
            }
            if (!validate_d3d11_slot(
                    binding,
                    slot,
                    D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                    "sampler")) {
                break;
            }
            auto* sampler = device.get_sampler(binding.sampler);
            ID3D11SamplerState* native = sampler ? sampler->sampler.Get() : nullptr;
            set_samplers(ctx, stage_mask, slot, &native);
            break;
        }
        case ResourceBinding::Kind::StorageBuffer:
            if (validate_d3d11_register_class(
                    binding,
                    TC_SHADER_D3D11_REGISTER_U,
                    "storage buffer")) {
                tc::Log::error(
                    "D3D11CommandList::bind_resource_set: storage buffers are not implemented");
            }
            break;
    }
}

void bind_bound_resource_binding(
    D3D11RenderDevice& device,
    ID3D11DeviceContext* ctx,
    const BoundResourceBinding& binding
) {
    const UINT slot = d3d11_slot(binding);
    const uint32_t stage_mask = effective_stage_mask(binding);
    switch (binding.value.kind) {
        case BoundResourceKind::UniformBuffer: {
            if (!validate_d3d11_placement(
                    binding,
                    D3D11RegisterClass::B,
                    "uniform buffer")) {
                break;
            }
            if (!validate_d3d11_slot(
                    binding,
                    slot,
                    D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                    "uniform buffer")) {
                break;
            }
            if (binding.value.offset != 0) {
                tc::Log::error(
                    "D3D11CommandList::bind_resource_set: uniform buffer "
                    "resource '%s' has unsupported offset=%llu",
                    binding.plan_entry.resource.name.c_str(),
                    static_cast<unsigned long long>(binding.value.offset));
                break;
            }
            auto* buf = device.get_buffer(binding.value.buffer);
            ID3D11Buffer* native = buf ? buf->buffer.Get() : nullptr;
            set_constant_buffers(ctx, stage_mask, slot, &native);
            break;
        }
        case BoundResourceKind::SampledTexture: {
            if (!validate_d3d11_placement(
                    binding,
                    D3D11RegisterClass::T,
                    "sampled texture")) {
                break;
            }
            if (!validate_d3d11_slot(
                    binding,
                    slot,
                    D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,
                    "sampled texture")) {
                break;
            }
            auto* tex = device.get_texture(binding.value.texture);
            ID3D11ShaderResourceView* srv = tex ? tex->srv.Get() : nullptr;
            set_shader_resources(ctx, stage_mask, slot, &srv);
            auto* sampler = device.get_sampler(binding.value.sampler);
            ID3D11SamplerState* native_sampler = sampler
                ? sampler->sampler.Get()
                : device.default_sampler_state();
            set_samplers(ctx, stage_mask, slot, &native_sampler);
            break;
        }
        case BoundResourceKind::Sampler: {
            if (!validate_d3d11_placement(
                    binding,
                    D3D11RegisterClass::S,
                    "sampler")) {
                break;
            }
            if (!validate_d3d11_slot(
                    binding,
                    slot,
                    D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                    "sampler")) {
                break;
            }
            auto* sampler = device.get_sampler(binding.value.sampler);
            ID3D11SamplerState* native = sampler ? sampler->sampler.Get() : nullptr;
            set_samplers(ctx, stage_mask, slot, &native);
            break;
        }
        case BoundResourceKind::StorageBuffer:
            if (validate_d3d11_placement(
                    binding,
                    D3D11RegisterClass::U,
                    "storage buffer")) {
                tc::Log::error(
                    "D3D11CommandList::bind_resource_set: storage buffers are not implemented");
            }
            break;
    }
}

} // namespace

D3D11CommandList::D3D11CommandList(D3D11RenderDevice& device)
    : device_(device), ctx_(device.immediate_context()) {
}

void D3D11CommandList::begin() {
}

void D3D11CommandList::end() {
}

void D3D11CommandList::begin_render_pass(const RenderPassDesc& pass) {
    clear_shader_resources(ctx_);

    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};
    UINT rtv_count = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    for (const auto& color : pass.colors) {
        if (rtv_count >= rtvs.size()) {
            tc::Log::error("D3D11CommandList::begin_render_pass: too many color attachments");
            break;
        }
        auto* tex = device_.get_texture(color.texture);
        if (!tex || !tex->rtv) {
            tc::Log::error("D3D11CommandList::begin_render_pass: invalid color attachment");
            continue;
        }
        rtvs[rtv_count++] = tex->rtv.Get();
        width = tex->desc.width;
        height = tex->desc.height;
        if (color.load == LoadOp::Clear) {
            ctx_->ClearRenderTargetView(tex->rtv.Get(), color.clear_color);
        }
    }

    ID3D11DepthStencilView* dsv = nullptr;
    if (pass.has_depth) {
        auto* depth = device_.get_texture(pass.depth.texture);
        if (depth && depth->dsv) {
            dsv = depth->dsv.Get();
            width = depth->desc.width;
            height = depth->desc.height;
            if (pass.depth.load == LoadOp::Clear) {
                ctx_->ClearDepthStencilView(
                    dsv,
                    D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                    pass.depth.clear_depth,
                    pass.depth.clear_stencil);
            }
        } else {
            tc::Log::error("D3D11CommandList::begin_render_pass: invalid depth attachment");
        }
    }

    ctx_->OMSetRenderTargets(rtv_count, rtvs.data(), dsv);
    if (width > 0 && height > 0) {
        set_viewport(0, 0, static_cast<int>(width), static_cast<int>(height));
        set_scissor(0, 0, static_cast<int>(width), static_cast<int>(height));
    }
}

void D3D11CommandList::end_render_pass() {
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> null_rtvs{};
    ctx_->OMSetRenderTargets(static_cast<UINT>(null_rtvs.size()), null_rtvs.data(), nullptr);
    clear_shader_resources(ctx_);
}

void D3D11CommandList::bind_pipeline(PipelineHandle pipeline) {
    auto* pipe = device_.get_pipeline(pipeline);
    if (!pipe) {
        return;
    }
    auto* vs = device_.get_shader(pipe->desc.vertex_shader);
    auto* fs = device_.get_shader(pipe->desc.fragment_shader);
    auto* gs = pipe->desc.geometry_shader ? device_.get_shader(pipe->desc.geometry_shader) : nullptr;

    current_pipeline_ = pipeline;
    ctx_->IASetInputLayout(pipe->input_layout.Get());
    ctx_->IASetPrimitiveTopology(d3d11::to_d3d_topology(pipe->desc.topology));
    ctx_->VSSetShader(vs ? vs->vertex_shader.Get() : nullptr, nullptr, 0);
    ctx_->PSSetShader(fs ? fs->pixel_shader.Get() : nullptr, nullptr, 0);
    ctx_->GSSetShader(gs ? gs->geometry_shader.Get() : nullptr, nullptr, 0);
    ctx_->RSSetState(pipe->raster_state.Get());
    ctx_->OMSetDepthStencilState(pipe->depth_stencil_state.Get(), 0);
    const float blend_factor[4] = {0, 0, 0, 0};
    ctx_->OMSetBlendState(pipe->blend_state.Get(), blend_factor, 0xffffffffu);
}

void D3D11CommandList::bind_resource_set(ResourceSetHandle set,
                                         uint32_t /*set_index*/,
                                         const uint32_t* dynamic_offsets,
                                         uint32_t dynamic_offset_count) {
    if (dynamic_offsets || dynamic_offset_count != 0) {
        tc::Log::error("D3D11CommandList::bind_resource_set: dynamic offsets are not implemented");
    }
    auto* rs = device_.get_resource_set(set);
    if (!rs) return;

    if (rs->has_bound_desc) {
        for (const ResourceBinding& binding : rs->legacy_numeric_bindings) {
            bind_legacy_resource_binding(device_, ctx_, binding);
        }
        for_each_dirty_bound_resource_binding(rs->bound_desc, [&](const BoundResourceBinding& binding) {
            bind_bound_resource_binding(device_, ctx_, binding);
        });
        return;
    }

    for (const auto& binding : rs->desc.bindings) {
        bind_legacy_resource_binding(device_, ctx_, binding);
    }
}

void D3D11CommandList::set_push_constants(const void* data, uint32_t size) {
    if (size == 0) {
        ID3D11Buffer* null_buffer = nullptr;
        ctx_->VSSetConstantBuffers(TGFX2_D3D11_PUSH_CONSTANTS_BINDING, 1, &null_buffer);
        ctx_->PSSetConstantBuffers(TGFX2_D3D11_PUSH_CONSTANTS_BINDING, 1, &null_buffer);
        ctx_->GSSetConstantBuffers(TGFX2_D3D11_PUSH_CONSTANTS_BINDING, 1, &null_buffer);
        return;
    }
    if (data == nullptr) {
        tc::Log::error("D3D11CommandList::set_push_constants: data is null for size=%u", size);
        return;
    }
    if (size > TGFX2_PUSH_CONSTANTS_MAX_BYTES) {
        tc::Log::error(
            "D3D11CommandList::set_push_constants: size=%u exceeds max=%u",
            size,
            TGFX2_PUSH_CONSTANTS_MAX_BYTES);
        return;
    }

    const uint32_t padded_size = std::max(16u, (size + 15u) & ~15u);
    if (!push_constant_buffer_ || push_constant_buffer_size_ < padded_size) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = padded_size;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        HRESULT hr = device_.native_device()->CreateBuffer(&desc, nullptr, buffer.GetAddressOf());
        if (FAILED(hr)) {
            tc::Log::error(
                "D3D11CommandList::set_push_constants: CreateBuffer failed HRESULT=0x%08X size=%u",
                static_cast<unsigned>(hr),
                padded_size);
            return;
        }
        push_constant_buffer_ = std::move(buffer);
        push_constant_buffer_size_ = padded_size;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = ctx_->Map(push_constant_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        tc::Log::error(
            "D3D11CommandList::set_push_constants: Map failed HRESULT=0x%08X size=%u",
            static_cast<unsigned>(hr),
            size);
        return;
    }
    std::memset(mapped.pData, 0, push_constant_buffer_size_);
    std::memcpy(mapped.pData, data, size);
    ctx_->Unmap(push_constant_buffer_.Get(), 0);

    ID3D11Buffer* native = push_constant_buffer_.Get();
    ctx_->VSSetConstantBuffers(TGFX2_D3D11_PUSH_CONSTANTS_BINDING, 1, &native);
    ctx_->PSSetConstantBuffers(TGFX2_D3D11_PUSH_CONSTANTS_BINDING, 1, &native);
    ctx_->GSSetConstantBuffers(TGFX2_D3D11_PUSH_CONSTANTS_BINDING, 1, &native);
}

void D3D11CommandList::bind_vertex_buffer(uint32_t slot, BufferHandle buffer, uint64_t offset) {
    auto* buf = device_.get_buffer(buffer);
    auto* pipe = device_.get_pipeline(current_pipeline_);
    if (!buf || !buf->buffer || !pipe || slot >= pipe->desc.vertex_layouts.size()) {
        return;
    }
    ID3D11Buffer* native = buf->buffer.Get();
    UINT stride = pipe->desc.vertex_layouts[slot].stride;
    UINT native_offset = static_cast<UINT>(offset);
    ctx_->IASetVertexBuffers(slot, 1, &native, &stride, &native_offset);
}

void D3D11CommandList::bind_index_buffer(BufferHandle buffer, IndexType type, uint64_t offset) {
    auto* buf = device_.get_buffer(buffer);
    if (!buf || !buf->buffer) return;
    ctx_->IASetIndexBuffer(buf->buffer.Get(), d3d11::to_dxgi_index_format(type), static_cast<UINT>(offset));
}

void D3D11CommandList::draw(uint32_t vertex_count, uint32_t first_vertex) {
    ctx_->Draw(vertex_count, first_vertex);
}

void D3D11CommandList::draw_instanced(uint32_t vertex_count,
                                      uint32_t instance_count,
                                      uint32_t first_vertex,
                                      uint32_t first_instance) {
    ctx_->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
}

void D3D11CommandList::draw_indexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset) {
    ctx_->DrawIndexed(index_count, first_index, vertex_offset);
}

void D3D11CommandList::draw_indexed_instanced(uint32_t index_count,
                                              uint32_t instance_count,
                                              uint32_t first_index,
                                              int32_t vertex_offset,
                                              uint32_t first_instance) {
    ctx_->DrawIndexedInstanced(index_count, instance_count, first_index, vertex_offset, first_instance);
}

void D3D11CommandList::dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
    ctx_->Dispatch(group_x, group_y, group_z);
}

void D3D11CommandList::copy_buffer(BufferHandle src, BufferHandle dst, uint64_t size,
                                   uint64_t src_offset, uint64_t dst_offset) {
    auto* s = device_.get_buffer(src);
    auto* d = device_.get_buffer(dst);
    if (!s || !d || !s->buffer || !d->buffer) return;
    D3D11_BOX box{};
    box.left = static_cast<UINT>(src_offset);
    box.right = static_cast<UINT>(src_offset + size);
    box.bottom = 1;
    box.back = 1;
    ctx_->CopySubresourceRegion(d->buffer.Get(), 0, static_cast<UINT>(dst_offset), 0, 0, s->buffer.Get(), 0, &box);
}

void D3D11CommandList::copy_texture(TextureHandle src, TextureHandle dst) {
    auto* s = device_.get_texture(src);
    auto* d = device_.get_texture(dst);
    if (!s || !d || !s->texture || !d->texture) return;

    const bool same_format = s->desc.format == d->desc.format;
    const bool same_samples = s->desc.sample_count == d->desc.sample_count;
    const bool same_extent = s->desc.width == d->desc.width &&
                             s->desc.height == d->desc.height;
    const bool msaa_to_single = s->desc.sample_count > 1 && d->desc.sample_count == 1;

    if (msaa_to_single && same_format && same_extent &&
        !d3d11::is_depth_format(s->desc.format)) {
        ctx_->ResolveSubresource(
            d->texture.Get(),
            0,
            s->texture.Get(),
            0,
            d3d11::to_dxgi_format(s->desc.format));
        return;
    }

    if (same_samples && same_format && same_extent) {
        ctx_->CopyResource(d->texture.Get(), s->texture.Get());
        return;
    }

    tc::Log::error(
        "D3D11CommandList::copy_texture: unsupported copy "
        "src=%ux%u samples=%u format=%d dst=%ux%u samples=%u format=%d",
        s->desc.width,
        s->desc.height,
        s->desc.sample_count,
        static_cast<int>(s->desc.format),
        d->desc.width,
        d->desc.height,
        d->desc.sample_count,
        static_cast<int>(d->desc.format));
}

void D3D11CommandList::set_viewport(int x, int y, int width, int height) {
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = static_cast<float>(x);
    viewport.TopLeftY = static_cast<float>(y);
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ctx_->RSSetViewports(1, &viewport);
}

void D3D11CommandList::set_scissor(int x, int y, int width, int height) {
    D3D11_RECT rect{};
    rect.left = x;
    rect.top = y;
    rect.right = x + width;
    rect.bottom = y + height;
    ctx_->RSSetScissorRects(1, &rect);
}

} // namespace tgfx
