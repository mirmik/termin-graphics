#include "tgfx2/webgpu/webgpu_command_list.hpp"

#include "tgfx2/webgpu/webgpu_render_device.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <tcbase/tc_log.h>

namespace tgfx {
namespace {

[[noreturn]] void command_fail(const char* message) {
    tc_log_error("WebGPU command list: %s", message);
    throw std::runtime_error(message);
}

wgpu::LoadOp load_op(LoadOp op) {
    switch (op) {
        case LoadOp::Load: return wgpu::LoadOp::Load;
        case LoadOp::Clear: return wgpu::LoadOp::Clear;
        case LoadOp::DontCare: return wgpu::LoadOp::Clear;
    }
    command_fail("unknown load operation");
}

wgpu::StoreOp store_op(StoreOp op) {
    return op == StoreOp::Store ? wgpu::StoreOp::Store : wgpu::StoreOp::Discard;
}

} // namespace

WebGpuCommandList::WebGpuCommandList(WebGpuRenderDevice& device) : device_(device) {}

void WebGpuCommandList::begin() {
    if (recording_) command_fail("begin called while recording");
    encoder_ = device_.device_.CreateCommandEncoder();
    command_buffer_ = nullptr;
    recording_ = true;
    current_layout_token_ = 0;
}

void WebGpuCommandList::end() {
    if (!recording_ || in_render_pass_) command_fail("end requires an ended render pass");
    command_buffer_ = encoder_.Finish();
    encoder_ = nullptr;
    recording_ = false;
}

void WebGpuCommandList::begin_render_pass(const RenderPassDesc& pass) {
    if (!recording_ || in_render_pass_) command_fail("invalid begin_render_pass state");
    std::vector<wgpu::RenderPassColorAttachment> colors(pass.colors.size());
    for (size_t index = 0; index < pass.colors.size(); ++index) {
        const ColorAttachmentDesc& source = pass.colors[index];
        const WebGpuTexture* texture = device_.textures_.get(source.texture.id);
        if (!texture) command_fail("render pass references an invalid color texture");
        colors[index].view = texture->view;
        colors[index].loadOp = load_op(source.load);
        colors[index].storeOp = store_op(source.store);
        colors[index].clearValue = {
            source.clear_color[0], source.clear_color[1],
            source.clear_color[2], source.clear_color[3]};
    }
    wgpu::RenderPassDepthStencilAttachment depth;
    if (pass.has_depth) {
        const WebGpuTexture* texture = device_.textures_.get(pass.depth.texture.id);
        if (!texture) command_fail("render pass references an invalid depth texture");
        depth.view = texture->view;
        depth.depthLoadOp = load_op(pass.depth.load);
        depth.depthStoreOp = store_op(pass.depth.store);
        depth.depthClearValue = pass.depth.clear_depth;
        depth.stencilLoadOp = wgpu::LoadOp::Undefined;
        depth.stencilStoreOp = wgpu::StoreOp::Undefined;
    }
    wgpu::RenderPassDescriptor native;
    native.colorAttachmentCount = colors.size();
    native.colorAttachments = colors.data();
    native.depthStencilAttachment = pass.has_depth ? &depth : nullptr;
    render_pass_ = encoder_.BeginRenderPass(&native);
    in_render_pass_ = true;
}

void WebGpuCommandList::end_render_pass() {
    if (!in_render_pass_) command_fail("no active render pass");
    render_pass_.End();
    render_pass_ = nullptr;
    in_render_pass_ = false;
}

void WebGpuCommandList::bind_pipeline(PipelineHandle handle) {
    if (!in_render_pass_) command_fail("bind_pipeline requires a render pass");
    const WebGpuPipeline* pipeline = device_.pipelines_.get(handle.id);
    if (!pipeline) command_fail("invalid pipeline handle");
    render_pass_.SetPipeline(pipeline->object);
    current_layout_token_ = pipeline->layout_token;
}

void WebGpuCommandList::bind_resource_set(
    ResourceSetHandle handle, uint32_t set_index,
    const uint32_t* dynamic_offsets, uint32_t dynamic_offset_count) {
    if (!in_render_pass_) command_fail("bind_resource_set requires a render pass");
    if (set_index != 0) command_fail("only WebGPU bind group 0 is supported");
    if (dynamic_offsets || dynamic_offset_count != 0) {
        command_fail("dynamic resource offsets are not supported");
    }
    const WebGpuResourceSet* set = device_.resource_sets_.get(handle.id);
    if (!set) command_fail("invalid resource set handle");
    if (current_layout_token_ == 0 || set->layout_token != current_layout_token_) {
        command_fail("resource set is incompatible with the bound pipeline");
    }
    render_pass_.SetBindGroup(0, set->object);
}

void WebGpuCommandList::set_push_constants(const void*, uint32_t) {
    command_fail("push constants are unsupported by WebGPU");
}

void WebGpuCommandList::bind_vertex_buffer(
    uint32_t slot, BufferHandle handle, uint64_t offset) {
    if (!in_render_pass_) command_fail("bind_vertex_buffer requires a render pass");
    const WebGpuBuffer* buffer = device_.buffers_.get(handle.id);
    if (!buffer || offset >= buffer->desc.size) command_fail("invalid vertex buffer");
    render_pass_.SetVertexBuffer(slot, buffer->object, offset, buffer->desc.size - offset);
}

void WebGpuCommandList::bind_index_buffer(
    BufferHandle handle, IndexType type, uint64_t offset) {
    if (!in_render_pass_) command_fail("bind_index_buffer requires a render pass");
    const WebGpuBuffer* buffer = device_.buffers_.get(handle.id);
    if (!buffer || offset >= buffer->desc.size) command_fail("invalid index buffer");
    render_pass_.SetIndexBuffer(
        buffer->object,
        type == IndexType::Uint16 ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32,
        offset, buffer->desc.size - offset);
}

void WebGpuCommandList::draw(uint32_t count, uint32_t first) {
    draw_instanced(count, 1, first, 0);
}

void WebGpuCommandList::draw_instanced(
    uint32_t count, uint32_t instances, uint32_t first, uint32_t first_instance) {
    if (!in_render_pass_) command_fail("draw requires a render pass");
    render_pass_.Draw(count, instances, first, first_instance);
}

void WebGpuCommandList::draw_indexed(
    uint32_t count, uint32_t first, int32_t vertex_offset) {
    draw_indexed_instanced(count, 1, first, vertex_offset, 0);
}

void WebGpuCommandList::draw_indexed_instanced(
    uint32_t count, uint32_t instances, uint32_t first,
    int32_t vertex_offset, uint32_t first_instance) {
    if (!in_render_pass_) command_fail("draw_indexed requires a render pass");
    render_pass_.DrawIndexed(count, instances, first, vertex_offset, first_instance);
}

void WebGpuCommandList::dispatch(uint32_t, uint32_t, uint32_t) {
    command_fail("compute dispatch is not represented by tgfx2 PipelineDesc yet");
}

void WebGpuCommandList::copy_buffer(
    BufferHandle src_handle, BufferHandle dst_handle, uint64_t size,
    uint64_t src_offset, uint64_t dst_offset) {
    if (!recording_ || in_render_pass_) command_fail("copy_buffer requires recording outside a pass");
    const WebGpuBuffer* src = device_.buffers_.get(src_handle.id);
    const WebGpuBuffer* dst = device_.buffers_.get(dst_handle.id);
    if (!src || !dst || src_offset + size > src->desc.size ||
        dst_offset + size > dst->desc.size) command_fail("invalid buffer copy");
    encoder_.CopyBufferToBuffer(src->object, src_offset, dst->object, dst_offset, size);
}

void WebGpuCommandList::copy_texture(TextureHandle src_handle, TextureHandle dst_handle) {
    if (!recording_ || in_render_pass_) command_fail("copy_texture requires recording outside a pass");
    const WebGpuTexture* src = device_.textures_.get(src_handle.id);
    const WebGpuTexture* dst = device_.textures_.get(dst_handle.id);
    if (!src || !dst || src->desc.format != dst->desc.format) {
        command_fail("texture copy requires valid equal-format textures");
    }
    wgpu::TexelCopyTextureInfo source;
    source.texture = src->object;
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = dst->object;
    wgpu::Extent3D extent{
        std::min(src->desc.width, dst->desc.width),
        std::min(src->desc.height, dst->desc.height), 1};
    encoder_.CopyTextureToTexture(&source, &destination, &extent);
}

void WebGpuCommandList::set_viewport(int x, int y, int width, int height) {
    if (!in_render_pass_ || x < 0 || y < 0 || width <= 0 || height <= 0) {
        command_fail("invalid viewport");
    }
    render_pass_.SetViewport(
        static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
}

void WebGpuCommandList::set_scissor(int x, int y, int width, int height) {
    if (!in_render_pass_ || x < 0 || y < 0 || width <= 0 || height <= 0) {
        command_fail("invalid scissor");
    }
    render_pass_.SetScissorRect(x, y, width, height);
}

wgpu::CommandBuffer WebGpuCommandList::take_command_buffer() {
    return std::exchange(command_buffer_, nullptr);
}

} // namespace tgfx
