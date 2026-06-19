#include "tgfx2/d3d11/d3d11_command_list.hpp"

#include "tgfx2/d3d11/d3d11_type_conversions.hpp"

#include <array>

#include <tcbase/tc_log.hpp>

namespace tgfx {

D3D11CommandList::D3D11CommandList(D3D11RenderDevice& device)
    : device_(device), ctx_(device.immediate_context()) {
}

void D3D11CommandList::begin() {
}

void D3D11CommandList::end() {
}

void D3D11CommandList::begin_render_pass(const RenderPassDesc& pass) {
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

    for (const auto& binding : rs->desc.bindings) {
        const UINT slot = binding.binding + binding.array_element;
        switch (binding.kind) {
            case ResourceBinding::Kind::UniformBuffer: {
                auto* buf = device_.get_buffer(binding.buffer);
                ID3D11Buffer* native = buf ? buf->buffer.Get() : nullptr;
                ctx_->VSSetConstantBuffers(slot, 1, &native);
                ctx_->PSSetConstantBuffers(slot, 1, &native);
                ctx_->GSSetConstantBuffers(slot, 1, &native);
                break;
            }
            case ResourceBinding::Kind::SampledTexture: {
                auto* tex = device_.get_texture(binding.texture);
                ID3D11ShaderResourceView* srv = tex ? tex->srv.Get() : nullptr;
                ctx_->VSSetShaderResources(slot, 1, &srv);
                ctx_->PSSetShaderResources(slot, 1, &srv);
                ctx_->GSSetShaderResources(slot, 1, &srv);
                auto* sampler = device_.get_sampler(binding.sampler);
                ID3D11SamplerState* native_sampler = sampler ? sampler->sampler.Get() : nullptr;
                if (native_sampler) {
                    ctx_->VSSetSamplers(slot, 1, &native_sampler);
                    ctx_->PSSetSamplers(slot, 1, &native_sampler);
                    ctx_->GSSetSamplers(slot, 1, &native_sampler);
                }
                break;
            }
            case ResourceBinding::Kind::Sampler: {
                auto* sampler = device_.get_sampler(binding.sampler);
                ID3D11SamplerState* native = sampler ? sampler->sampler.Get() : nullptr;
                ctx_->VSSetSamplers(slot, 1, &native);
                ctx_->PSSetSamplers(slot, 1, &native);
                ctx_->GSSetSamplers(slot, 1, &native);
                break;
            }
            case ResourceBinding::Kind::StorageBuffer:
                tc::Log::error("D3D11CommandList::bind_resource_set: storage buffers are not implemented");
                break;
        }
    }
}

void D3D11CommandList::set_push_constants(const void* /*data*/, uint32_t size) {
    if (size != 0) {
        tc::Log::error("D3D11CommandList::set_push_constants: not implemented");
    }
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
    ctx_->CopyResource(d->texture.Get(), s->texture.Get());
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
