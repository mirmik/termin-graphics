#include "tgfx2/webgpu/webgpu_render_device.hpp"

#include "tgfx2/webgpu/webgpu_command_list.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <tcbase/tc_log.h>
#include <tcbase/trent/json.h>

extern "C" {
#include "tgfx/resources/tc_mesh_registry.h"
#include "tgfx/resources/tc_shader_registry.h"
#include "tgfx/resources/tc_texture_registry.h"
}

namespace {

void webgpu_invalidate_tc_shader(uint32_t pool_index, void* user) {
    static_cast<tgfx::WebGpuRenderDevice*>(user)->invalidate_tc_shader_cache(pool_index);
}

void webgpu_invalidate_tc_texture(uint32_t pool_index, void* user) {
    static_cast<tgfx::WebGpuRenderDevice*>(user)->invalidate_tc_texture_cache(pool_index);
}

void webgpu_invalidate_tc_mesh(uint32_t pool_index, void* user) {
    static_cast<tgfx::WebGpuRenderDevice*>(user)->invalidate_tc_mesh_cache(pool_index);
}

} // namespace

namespace tgfx {
namespace {

[[noreturn]] void fail(const std::string& message) {
    tc_log_error("WebGPU: %s", message.c_str());
    throw std::runtime_error(message);
}

const nos::trent* field(const nos::trent& object, const char* name) {
    return object.is_dict() ? object._get(name) : nullptr;
}

bool uint_field(const nos::trent& object, const char* name, uint32_t& value) {
    const nos::trent* item = field(object, name);
    if (!item || !item->is_numer()) return false;
    const int64_t integer = item->as_integer();
    if (integer < 0 || integer > std::numeric_limits<uint32_t>::max()) return false;
    value = static_cast<uint32_t>(integer);
    return true;
}

bool string_field(const nos::trent& object, const char* name, std::string& value) {
    const nos::trent* item = field(object, name);
    if (!item || !item->is_string()) return false;
    value = item->as_string();
    return true;
}

ShaderResourceKind resource_kind(const std::string& name) {
    if (name == "constant_buffer") return ShaderResourceKind::ConstantBuffer;
    if (name == "texture") return ShaderResourceKind::Texture;
    if (name == "sampler") return ShaderResourceKind::Sampler;
    if (name == "storage_buffer") return ShaderResourceKind::StorageBuffer;
    if (name == "storage_texture") return ShaderResourceKind::StorageTexture;
    return ShaderResourceKind::None;
}

std::vector<WebGpuLayoutEntry> parse_layout(const ShaderDesc& desc) {
    if (desc.resource_layout_json.empty()) return {};
    nos::trent root;
    try {
        root = nos::json::parse(desc.resource_layout_json);
    } catch (const std::exception& error) {
        fail("invalid layout sidecar for '" + desc.debug_name + "': " + error.what());
    }
    uint32_t version = 0;
    std::string target;
    if (!uint_field(root, "version", version) || version != 3 ||
        !string_field(root, "target", target) || target != "webgpu") {
        fail("shader '" + desc.debug_name +
             "' requires a WebGPU resource layout sidecar version 3");
    }
    const nos::trent* resources = field(root, "resources");
    if (!resources || !resources->is_list()) {
        fail("shader '" + desc.debug_name + "' layout has no resources array");
    }

    std::vector<WebGpuLayoutEntry> result;
    for (const nos::trent& item : resources->as_list()) {
        std::string kind_name;
        WebGpuLayoutEntry entry;
        const nos::trent* placement = field(item, "webgpu");
        uint32_t group = 0;
        if (!string_field(item, "name", entry.name) ||
            !string_field(item, "kind", kind_name) ||
            !uint_field(item, "stage_mask", entry.stage_mask) ||
            !placement || !placement->is_dict() ||
            !uint_field(*placement, "group", group) || group != 0 ||
            !uint_field(*placement, "binding", entry.binding)) {
            fail("shader '" + desc.debug_name + "' has malformed WebGPU placement");
        }
        entry.kind = resource_kind(kind_name);
        if (entry.kind == ShaderResourceKind::None || entry.stage_mask == 0) {
            fail("shader '" + desc.debug_name + "' has unsupported layout resource kind");
        }
        uint_field(item, "size", entry.size);
        entry.has_sampler_binding =
            uint_field(*placement, "sampler_binding", entry.sampler_binding);
        if (entry.has_sampler_binding &&
            (entry.kind != ShaderResourceKind::Texture ||
             entry.sampler_binding == entry.binding)) {
            fail("shader '" + desc.debug_name + "' has invalid sampler_binding");
        }
        result.push_back(std::move(entry));
    }
    return result;
}

wgpu::ShaderStage shader_visibility(uint32_t mask) {
    wgpu::ShaderStage result = wgpu::ShaderStage::None;
    if (mask & (1u << 0)) result |= wgpu::ShaderStage::Vertex;
    if (mask & (1u << 1)) result |= wgpu::ShaderStage::Fragment;
    if (mask & (1u << 3)) result |= wgpu::ShaderStage::Compute;
    return result;
}

wgpu::BufferUsage buffer_usage(const BufferDesc& desc) {
    wgpu::BufferUsage usage = wgpu::BufferUsage::None;
    if (has_flag(desc.usage, BufferUsage::Vertex)) usage |= wgpu::BufferUsage::Vertex;
    if (has_flag(desc.usage, BufferUsage::Index)) usage |= wgpu::BufferUsage::Index;
    if (has_flag(desc.usage, BufferUsage::Uniform)) usage |= wgpu::BufferUsage::Uniform;
    if (has_flag(desc.usage, BufferUsage::Storage)) usage |= wgpu::BufferUsage::Storage;
    if (has_flag(desc.usage, BufferUsage::CopySrc)) usage |= wgpu::BufferUsage::CopySrc;
    if (has_flag(desc.usage, BufferUsage::CopyDst)) usage |= wgpu::BufferUsage::CopyDst;
    // WebGPU has no synchronous host-visible buffer mode. The portable
    // cpu_visible flag means that the runtime will update this buffer from
    // CPU memory, which maps naturally to Queue::WriteBuffer + CopyDst.
    if (desc.cpu_visible) usage |= wgpu::BufferUsage::CopyDst;
    return usage;
}

wgpu::TextureUsage texture_usage(TextureUsage value) {
    wgpu::TextureUsage usage = wgpu::TextureUsage::None;
    if (has_flag(value, TextureUsage::Sampled)) usage |= wgpu::TextureUsage::TextureBinding;
    if (has_flag(value, TextureUsage::Storage)) usage |= wgpu::TextureUsage::StorageBinding;
    if (has_flag(value, TextureUsage::ColorAttachment) ||
        has_flag(value, TextureUsage::DepthStencilAttachment)) {
        usage |= wgpu::TextureUsage::RenderAttachment;
    }
    if (has_flag(value, TextureUsage::CopySrc)) usage |= wgpu::TextureUsage::CopySrc;
    if (has_flag(value, TextureUsage::CopyDst)) usage |= wgpu::TextureUsage::CopyDst;
    return usage;
}

wgpu::TextureFormat texture_format(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8_UNorm: return wgpu::TextureFormat::R8Unorm;
        case PixelFormat::RG8_UNorm: return wgpu::TextureFormat::RG8Unorm;
        case PixelFormat::RGBA8_UNorm: return wgpu::TextureFormat::RGBA8Unorm;
        case PixelFormat::BGRA8_UNorm: return wgpu::TextureFormat::BGRA8Unorm;
        case PixelFormat::R16F: return wgpu::TextureFormat::R16Float;
        case PixelFormat::RG16F: return wgpu::TextureFormat::RG16Float;
        case PixelFormat::RGBA16F: return wgpu::TextureFormat::RGBA16Float;
        case PixelFormat::R32F: return wgpu::TextureFormat::R32Float;
        case PixelFormat::RG32F: return wgpu::TextureFormat::RG32Float;
        case PixelFormat::RGBA32F: return wgpu::TextureFormat::RGBA32Float;
        case PixelFormat::D24_UNorm: return wgpu::TextureFormat::Depth24Plus;
        case PixelFormat::D24_UNorm_S8_UInt: return wgpu::TextureFormat::Depth24PlusStencil8;
        case PixelFormat::D32F: return wgpu::TextureFormat::Depth32Float;
        case PixelFormat::RGBA8_sRGB: return wgpu::TextureFormat::RGBA8UnormSrgb;
        case PixelFormat::BGRA8_sRGB: return wgpu::TextureFormat::BGRA8UnormSrgb;
        case PixelFormat::RGB8_UNorm:
        case PixelFormat::Undefined:
            fail("unsupported pixel format");
    }
    fail("unknown pixel format");
}

uint32_t bytes_per_pixel(PixelFormat format) {
    switch (format) {
        case PixelFormat::R8_UNorm: return 1;
        case PixelFormat::RG8_UNorm: return 2;
        case PixelFormat::RGBA8_UNorm:
        case PixelFormat::BGRA8_UNorm:
        case PixelFormat::R32F:
        case PixelFormat::D32F:
        case PixelFormat::RGBA8_sRGB:
        case PixelFormat::BGRA8_sRGB: return 4;
        case PixelFormat::R16F: return 2;
        case PixelFormat::RG16F: return 4;
        case PixelFormat::RGBA16F:
        case PixelFormat::RG32F: return 8;
        case PixelFormat::RGBA32F: return 16;
        default: fail("texture upload format is unsupported");
    }
}

wgpu::FilterMode filter(FilterMode mode) {
    return mode == FilterMode::Nearest ? wgpu::FilterMode::Nearest : wgpu::FilterMode::Linear;
}

wgpu::MipmapFilterMode mip_filter(FilterMode mode) {
    return mode == FilterMode::Nearest ? wgpu::MipmapFilterMode::Nearest : wgpu::MipmapFilterMode::Linear;
}

wgpu::AddressMode address(AddressMode mode) {
    switch (mode) {
        case AddressMode::Repeat: return wgpu::AddressMode::Repeat;
        case AddressMode::MirroredRepeat: return wgpu::AddressMode::MirrorRepeat;
        case AddressMode::ClampToEdge: return wgpu::AddressMode::ClampToEdge;
        case AddressMode::ClampToBorder: fail("ClampToBorder samplers are unsupported by WebGPU");
    }
    fail("unknown sampler address mode");
}

wgpu::CompareFunction compare(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return wgpu::CompareFunction::Never;
        case CompareOp::Less: return wgpu::CompareFunction::Less;
        case CompareOp::Equal: return wgpu::CompareFunction::Equal;
        case CompareOp::LessEqual: return wgpu::CompareFunction::LessEqual;
        case CompareOp::Greater: return wgpu::CompareFunction::Greater;
        case CompareOp::NotEqual: return wgpu::CompareFunction::NotEqual;
        case CompareOp::GreaterEqual: return wgpu::CompareFunction::GreaterEqual;
        case CompareOp::Always: return wgpu::CompareFunction::Always;
    }
    fail("unknown compare operation");
}

wgpu::VertexFormat vertex_format(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float: return wgpu::VertexFormat::Float32;
        case VertexFormat::Float2: return wgpu::VertexFormat::Float32x2;
        case VertexFormat::Float3: return wgpu::VertexFormat::Float32x3;
        case VertexFormat::Float4: return wgpu::VertexFormat::Float32x4;
        case VertexFormat::Int: return wgpu::VertexFormat::Sint32;
        case VertexFormat::Int2: return wgpu::VertexFormat::Sint32x2;
        case VertexFormat::Int3: return wgpu::VertexFormat::Sint32x3;
        case VertexFormat::Int4: return wgpu::VertexFormat::Sint32x4;
        case VertexFormat::UInt: return wgpu::VertexFormat::Uint32;
        case VertexFormat::UInt2: return wgpu::VertexFormat::Uint32x2;
        case VertexFormat::UInt3: return wgpu::VertexFormat::Uint32x3;
        case VertexFormat::UInt4: return wgpu::VertexFormat::Uint32x4;
        case VertexFormat::Short2: return wgpu::VertexFormat::Sint16x2;
        case VertexFormat::Short4: return wgpu::VertexFormat::Sint16x4;
        case VertexFormat::UShort2: return wgpu::VertexFormat::Uint16x2;
        case VertexFormat::UShort4: return wgpu::VertexFormat::Uint16x4;
        case VertexFormat::Byte4: return wgpu::VertexFormat::Sint8x4;
        case VertexFormat::UByte4: return wgpu::VertexFormat::Uint8x4;
        case VertexFormat::UByte4N: return wgpu::VertexFormat::Unorm8x4;
        case VertexFormat::Short:
        case VertexFormat::Short3:
        case VertexFormat::UShort:
        case VertexFormat::UShort3:
            fail("WebGPU vertex formats cannot have one or three 16-bit components");
    }
    fail("unknown vertex format");
}

wgpu::PrimitiveTopology topology(PrimitiveTopology value) {
    switch (value) {
        case PrimitiveTopology::PointList: return wgpu::PrimitiveTopology::PointList;
        case PrimitiveTopology::LineList: return wgpu::PrimitiveTopology::LineList;
        case PrimitiveTopology::LineStrip: return wgpu::PrimitiveTopology::LineStrip;
        case PrimitiveTopology::TriangleList: return wgpu::PrimitiveTopology::TriangleList;
        case PrimitiveTopology::TriangleStrip: return wgpu::PrimitiveTopology::TriangleStrip;
    }
    fail("unknown primitive topology");
}

} // namespace

void WebGpuRenderDevice::request_async(
    const WebGpuDeviceRequest& request, WebGpuDeviceCallback callback) {
    if (!callback) fail("request_async requires a callback");
    const wgpu::Instance instance = wgpuCreateInstance(nullptr);
    if (!instance) {
        callback(nullptr, "wgpuCreateInstance failed");
        return;
    }
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas;
    canvas.selector = request.canvas_selector;
    wgpu::SurfaceDescriptor surface_desc;
    surface_desc.nextInChain = &canvas;
    const wgpu::Surface surface = instance.CreateSurface(&surface_desc);
    if (!surface) {
        callback(nullptr, "WebGPU canvas surface creation failed");
        return;
    }
    wgpu::RequestAdapterOptions options;
    options.compatibleSurface = surface;
    instance.RequestAdapter(
        &options, wgpu::CallbackMode::AllowSpontaneous,
        [instance, surface, request, callback = std::move(callback)](
            wgpu::RequestAdapterStatus status, wgpu::Adapter adapter,
            wgpu::StringView message) mutable {
            if (status != wgpu::RequestAdapterStatus::Success) {
                callback(nullptr, "WebGPU adapter request failed: " +
                    std::string(message.data, message.length));
                return;
            }
            wgpu::DeviceDescriptor device_desc;
            auto error_state = std::make_shared<WebGpuErrorState>();
            device_desc.SetUncapturedErrorCallback(
                [](const wgpu::Device&, wgpu::ErrorType type,
                   wgpu::StringView error, WebGpuErrorState* state) {
                    state->failed = true;
                    state->message.assign(error.data, error.length);
                    tc_log_error("WebGPU uncaptured error (%u): %.*s",
                        static_cast<unsigned>(type),
                        static_cast<int>(error.length), error.data);
                }, error_state.get());
            device_desc.SetDeviceLostCallback(
                wgpu::CallbackMode::AllowSpontaneous,
                [](const wgpu::Device&, wgpu::DeviceLostReason reason,
                   wgpu::StringView error, WebGpuErrorState* state) {
                    if (reason == wgpu::DeviceLostReason::Destroyed) return;
                    state->failed = true;
                    state->message = "device lost: " +
                        std::string(error.data, error.length);
                    tc_log_error("WebGPU device lost (%u): %.*s",
                        static_cast<unsigned>(reason),
                        static_cast<int>(error.length), error.data);
                }, error_state.get());
            adapter.RequestDevice(
                &device_desc, wgpu::CallbackMode::AllowSpontaneous,
                [instance, surface, adapter, request, error_state,
                 callback = std::move(callback)](
                    wgpu::RequestDeviceStatus device_status,
                    wgpu::Device device, wgpu::StringView device_message) mutable {
                    if (device_status != wgpu::RequestDeviceStatus::Success) {
                        callback(nullptr, "WebGPU device request failed: " +
                            std::string(device_message.data, device_message.length));
                        return;
                    }
                    try {
                        callback(std::unique_ptr<WebGpuRenderDevice>(
                            new WebGpuRenderDevice(instance, adapter, device, surface,
                                request.width, request.height, error_state)), {});
                    } catch (const std::exception& error) {
                        callback(nullptr, error.what());
                    }
                });
        });
}

WebGpuRenderDevice::WebGpuRenderDevice(
    wgpu::Instance instance, wgpu::Adapter adapter, wgpu::Device device,
    wgpu::Surface surface, uint32_t width, uint32_t height,
    std::shared_ptr<WebGpuErrorState> error_state)
    : error_state_(std::move(error_state)), instance_(std::move(instance)), adapter_(std::move(adapter)),
      device_(std::move(device)), queue_(device_.GetQueue()),
      surface_(std::move(surface)) {
    wgpu::SurfaceCapabilities surface_caps;
    if (surface_.GetCapabilities(adapter_, &surface_caps) != wgpu::Status::Success ||
        surface_caps.formatCount == 0) {
        fail("canvas surface has no supported formats");
    }
    surface_format_ = surface_caps.formats[0];
    for (size_t index = 0; index < surface_caps.formatCount; ++index) {
        if (surface_caps.formats[index] == wgpu::TextureFormat::RGBA8Unorm) {
            // Termin's canonical scene/display render targets are RGBA8. Keep
            // the browser surface copy-compatible when the adapter exposes it.
            surface_format_ = wgpu::TextureFormat::RGBA8Unorm;
            break;
        }
    }
    caps_.backend = BackendType::WebGPU;
    caps_.supports_compute = false;
    caps_.supports_geometry_shaders = false;
    caps_.supports_timestamp_queries = false;
    caps_.supports_multisample_resolve = true;
    caps_.supports_dynamic_uniform_offsets = false;
    caps_.supports_storage_textures = false;
    caps_.max_color_attachments = 4;
    caps_.max_texture_dimension_2d = 8192;
    caps_.max_texture_units = 16;
    configure_surface(width, height);
    default_sampler_ = create_sampler({});
    tc_shader_registry_add_destroy_hook(&webgpu_invalidate_tc_shader, this);
    tc_texture_registry_add_destroy_hook(&webgpu_invalidate_tc_texture, this);
    tc_mesh_registry_add_destroy_hook(&webgpu_invalidate_tc_mesh, this);
}

WebGpuRenderDevice::~WebGpuRenderDevice() {
    tc_mesh_registry_remove_destroy_hook(&webgpu_invalidate_tc_mesh, this);
    tc_texture_registry_remove_destroy_hook(&webgpu_invalidate_tc_texture, this);
    tc_shader_registry_remove_destroy_hook(&webgpu_invalidate_tc_shader, this);
    for (auto& [_, entry] : tc_mesh_cache_) {
        if (entry.vbo) destroy(entry.vbo);
        if (entry.ebo) destroy(entry.ebo);
    }
    for (auto& [_, entry] : tc_texture_cache_) {
        if (entry.handle) destroy(entry.handle);
    }
    for (auto& [_, entry] : tc_shader_cache_) {
        if (entry.vs) destroy(entry.vs);
        if (entry.fs) destroy(entry.fs);
    }
    if (default_sampler_) destroy(default_sampler_);
    if (surface_) surface_.Unconfigure();
}

void WebGpuRenderDevice::wait_idle() {
    tc_log_warn("WebGPU: wait_idle is asynchronous and intentionally a no-op");
}

void WebGpuRenderDevice::clear_texture(
    TextureHandle dst_handle,
    termin::Color4 color,
    termin::Bounds2i viewport) {
    const WebGpuTexture* dst = textures_.get(dst_handle.id);
    if (!dst) fail("clear_texture requires a valid destination texture");
    const int width = static_cast<int>(dst->desc.width);
    const int height = static_cast<int>(dst->desc.height);
    if (viewport.x0 != 0 || viewport.y0 != 0 ||
            viewport.x1 != width || viewport.y1 != height) {
        fail("WebGPU clear_texture currently requires a full-texture viewport");
    }
    RenderPassDesc pass;
    ColorAttachmentDesc attachment;
    attachment.texture = dst_handle;
    attachment.clear_color[0] = color.r;
    attachment.clear_color[1] = color.g;
    attachment.clear_color[2] = color.b;
    attachment.clear_color[3] = color.a;
    pass.colors.push_back(attachment);
    std::unique_ptr<ICommandList> commands = create_command_list();
    commands->begin();
    commands->begin_render_pass(pass);
    commands->end_render_pass();
    commands->end();
    submit(*commands);
}

void WebGpuRenderDevice::blit_to_texture(
    TextureHandle dst_handle,
    TextureHandle src_handle,
    termin::Bounds2i src_rect,
    termin::Bounds2i dst_rect) {
    const WebGpuTexture* src = textures_.get(src_handle.id);
    const WebGpuTexture* dst = textures_.get(dst_handle.id);
    if (!src || !dst) fail("blit_to_texture requires valid textures");
    const bool full_source = src_rect.x0 == 0 && src_rect.y0 == 0 &&
        src_rect.x1 == static_cast<int>(src->desc.width) &&
        src_rect.y1 == static_cast<int>(src->desc.height);
    const bool full_destination = dst_rect.x0 == 0 && dst_rect.y0 == 0 &&
        dst_rect.x1 == static_cast<int>(dst->desc.width) &&
        dst_rect.y1 == static_cast<int>(dst->desc.height);
    if (!full_source || !full_destination ||
            src->desc.width != dst->desc.width ||
            src->desc.height != dst->desc.height ||
            src->desc.sample_count != 1 || dst->desc.sample_count != 1) {
        fail("WebGPU blit_to_texture requires equal single-sample full textures");
    }
    std::unique_ptr<ICommandList> commands = create_command_list();
    commands->begin();
    commands->copy_texture(src_handle, dst_handle);
    commands->end();
    submit(*commands);
}

BufferHandle WebGpuRenderDevice::create_buffer(const BufferDesc& desc) {
    if (desc.size == 0) fail("create_buffer requires non-zero size");
    wgpu::BufferDescriptor native;
    native.size = desc.size;
    native.usage = buffer_usage(desc);
    if (native.usage == wgpu::BufferUsage::None) fail("create_buffer requires usage flags");
    wgpu::Buffer object = device_.CreateBuffer(&native);
    if (!object) fail("CreateBuffer failed");
    return {buffers_.add({std::move(object), desc})};
}

TextureHandle WebGpuRenderDevice::create_texture(const TextureDesc& desc) {
    if (desc.array_layers != 1) {
        tc_log(TC_LOG_ERROR,
               "WebGpuRenderDevice::create_texture: layered textures are not supported");
        return {};
    }
    if (desc.width == 0 || desc.height == 0 || desc.mip_levels == 0) {
        fail("create_texture requires a non-zero extent and mip count");
    }
    wgpu::TextureDescriptor native;
    native.size = {desc.width, desc.height, 1};
    native.mipLevelCount = desc.mip_levels;
    native.sampleCount = desc.sample_count;
    native.dimension = wgpu::TextureDimension::e2D;
    native.format = texture_format(desc.format);
    native.usage = texture_usage(desc.usage);
    if (native.usage == wgpu::TextureUsage::None) fail("create_texture requires usage flags");
    wgpu::Texture object = device_.CreateTexture(&native);
    if (!object) fail("CreateTexture failed");
    wgpu::TextureView view = object.CreateView();
    return {textures_.add({std::move(object), std::move(view), desc, false})};
}

SamplerHandle WebGpuRenderDevice::create_sampler(const SamplerDesc& desc) {
    wgpu::SamplerDescriptor native;
    native.minFilter = filter(desc.min_filter);
    native.magFilter = filter(desc.mag_filter);
    native.mipmapFilter = mip_filter(desc.mip_filter);
    native.addressModeU = address(desc.address_u);
    native.addressModeV = address(desc.address_v);
    native.addressModeW = address(desc.address_w);
    native.maxAnisotropy = static_cast<uint16_t>(std::max(1.0f, desc.max_anisotropy));
    if (desc.compare_enable) native.compare = compare(desc.compare_op);
    wgpu::Sampler object = device_.CreateSampler(&native);
    if (!object) fail("CreateSampler failed");
    return {samplers_.add({std::move(object)})};
}

ShaderHandle WebGpuRenderDevice::create_shader(const ShaderDesc& desc) {
    if (desc.stage == ShaderStage::Geometry) fail("geometry shaders are unsupported");
    if (desc.source.empty()) fail("WebGPU shader requires prebuilt WGSL source");
    wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = std::string_view(desc.source);
    wgpu::ShaderModuleDescriptor native;
    native.nextInChain = &wgsl;
    native.label = std::string_view(desc.debug_name);
    wgpu::ShaderModule object = device_.CreateShaderModule(&native);
    if (!object) fail("CreateShaderModule failed for '" + desc.debug_name + "'");
    return {shaders_.add({std::move(object), desc, parse_layout(desc)})};
}

PipelineHandle WebGpuRenderDevice::create_pipeline(const PipelineDesc& desc) {
    const WebGpuShader* vertex = shaders_.get(desc.vertex_shader.id);
    const WebGpuShader* fragment = shaders_.get(desc.fragment_shader.id);
    if (!vertex || !fragment) fail("pipeline references invalid shaders");
    if (desc.geometry_shader) fail("geometry shaders are unsupported");

    std::vector<WebGpuLayoutEntry> layout = vertex->layout;
    for (const WebGpuLayoutEntry& incoming : fragment->layout) {
        auto existing = std::find_if(layout.begin(), layout.end(), [&](const auto& item) {
            return item.binding == incoming.binding;
        });
        if (existing == layout.end()) {
            layout.push_back(incoming);
        } else if (existing->name != incoming.name || existing->kind != incoming.kind ||
                   existing->has_sampler_binding != incoming.has_sampler_binding ||
                   (existing->has_sampler_binding &&
                    existing->sampler_binding != incoming.sampler_binding)) {
            fail("shader stages disagree on WebGPU resource placement");
        } else {
            existing->stage_mask |= incoming.stage_mask;
        }
    }

    std::vector<wgpu::BindGroupLayoutEntry> native_layout;
    for (const WebGpuLayoutEntry& item : layout) {
        if (item.kind == ShaderResourceKind::StorageTexture) {
            fail("storage textures are not enabled in this WebGPU slice");
        }
        wgpu::BindGroupLayoutEntry entry;
        entry.binding = item.binding;
        entry.visibility = shader_visibility(item.stage_mask);
        switch (item.kind) {
            case ShaderResourceKind::ConstantBuffer:
                entry.buffer.type = wgpu::BufferBindingType::Uniform;
                entry.buffer.minBindingSize = item.size;
                break;
            case ShaderResourceKind::StorageBuffer:
                entry.buffer.type = wgpu::BufferBindingType::Storage;
                entry.buffer.minBindingSize = item.size;
                break;
            case ShaderResourceKind::Texture:
                entry.texture.sampleType = wgpu::TextureSampleType::Float;
                break;
            case ShaderResourceKind::Sampler:
                entry.sampler.type = wgpu::SamplerBindingType::Filtering;
                break;
            default: fail("unsupported WebGPU resource layout kind");
        }
        native_layout.push_back(entry);
        if (item.has_sampler_binding) {
            wgpu::BindGroupLayoutEntry sampler;
            sampler.binding = item.sampler_binding;
            sampler.visibility = entry.visibility;
            sampler.sampler.type = wgpu::SamplerBindingType::Filtering;
            native_layout.push_back(sampler);
        }
    }
    std::sort(native_layout.begin(), native_layout.end(),
        [](const auto& a, const auto& b) { return a.binding < b.binding; });
    for (size_t index = 1; index < native_layout.size(); ++index) {
        if (native_layout[index - 1].binding == native_layout[index].binding) {
            fail("WebGPU bind group contains a binding collision");
        }
    }
    wgpu::BindGroupLayoutDescriptor bgl_desc;
    bgl_desc.entryCount = native_layout.size();
    bgl_desc.entries = native_layout.data();
    wgpu::BindGroupLayout bind_group_layout = device_.CreateBindGroupLayout(&bgl_desc);
    wgpu::PipelineLayoutDescriptor pipeline_layout_desc;
    pipeline_layout_desc.bindGroupLayoutCount = 1;
    pipeline_layout_desc.bindGroupLayouts = &bind_group_layout;
    wgpu::PipelineLayout pipeline_layout = device_.CreatePipelineLayout(&pipeline_layout_desc);

    std::vector<std::vector<wgpu::VertexAttribute>> attributes(desc.vertex_layouts.size());
    std::vector<wgpu::VertexBufferLayout> buffers(desc.vertex_layouts.size());
    for (size_t i = 0; i < desc.vertex_layouts.size(); ++i) {
        const VertexLayoutDesc& source = desc.vertex_layouts[i];
        if (source.use_shader_input_locations) {
            fail("WebGPU requires explicit vertex input locations");
        }
        attributes[i].reserve(source.attribute_count);
        for (uint32_t j = 0; j < source.attribute_count; ++j) {
            wgpu::VertexAttribute attribute;
            attribute.shaderLocation = source.attributes[j].location;
            attribute.offset = source.attributes[j].offset;
            attribute.format = vertex_format(source.attributes[j].format);
            attributes[i].push_back(attribute);
        }
        buffers[i].arrayStride = source.stride;
        buffers[i].stepMode = source.per_instance
            ? wgpu::VertexStepMode::Instance : wgpu::VertexStepMode::Vertex;
        buffers[i].attributeCount = attributes[i].size();
        buffers[i].attributes = attributes[i].data();
    }

    std::vector<wgpu::ColorTargetState> color_targets(desc.color_formats.size());
    for (size_t i = 0; i < color_targets.size(); ++i) {
        color_targets[i].format = texture_format(desc.color_formats[i]);
        color_targets[i].writeMask = static_cast<wgpu::ColorWriteMask>(
            (desc.color_mask.r ? static_cast<uint64_t>(wgpu::ColorWriteMask::Red) : 0) |
            (desc.color_mask.g ? static_cast<uint64_t>(wgpu::ColorWriteMask::Green) : 0) |
            (desc.color_mask.b ? static_cast<uint64_t>(wgpu::ColorWriteMask::Blue) : 0) |
            (desc.color_mask.a ? static_cast<uint64_t>(wgpu::ColorWriteMask::Alpha) : 0));
        if (desc.blend.enabled) fail("WebGPU blend mapping is not implemented yet");
    }
    wgpu::FragmentState fragment_state;
    fragment_state.module = fragment->object;
    fragment_state.entryPoint = std::string_view(fragment->desc.entry_point);
    fragment_state.targetCount = color_targets.size();
    fragment_state.targets = color_targets.data();

    wgpu::DepthStencilState depth_state;
    wgpu::RenderPipelineDescriptor native;
    native.layout = pipeline_layout;
    native.vertex.module = vertex->object;
    native.vertex.entryPoint = std::string_view(vertex->desc.entry_point);
    native.vertex.bufferCount = buffers.size();
    native.vertex.buffers = buffers.data();
    native.fragment = &fragment_state;
    native.primitive.topology = topology(desc.topology);
    native.primitive.frontFace = desc.raster.front_face == FrontFace::CCW
        ? wgpu::FrontFace::CCW : wgpu::FrontFace::CW;
    native.primitive.cullMode = desc.raster.cull == CullMode::None
        ? wgpu::CullMode::None
        : (desc.raster.cull == CullMode::Front ? wgpu::CullMode::Front : wgpu::CullMode::Back);
    native.multisample.count = desc.sample_count;
    if (desc.depth_format != PixelFormat::Undefined) {
        depth_state.format = texture_format(desc.depth_format);
        depth_state.depthWriteEnabled = desc.depth_stencil.depth_write;
        depth_state.depthCompare = desc.depth_stencil.depth_test
            ? compare(desc.depth_stencil.depth_compare) : wgpu::CompareFunction::Always;
        native.depthStencil = &depth_state;
    }
    wgpu::RenderPipeline object = device_.CreateRenderPipeline(&native);
    if (!object) fail("CreateRenderPipeline failed");
    WebGpuPipeline pipeline{std::move(object), std::move(bind_group_layout),
                            desc, 0, std::move(layout)};
    const uint32_t id = pipelines_.add(std::move(pipeline));
    pipelines_.get(id)->layout_token = id;
    return {id};
}

ResourceSetHandle WebGpuRenderDevice::create_bound_resource_set(
    const BoundResourceSetDesc& desc) {
    WebGpuPipeline* pipeline = pipelines_.get(static_cast<uint32_t>(desc.resource_layout_token));
    if (!pipeline || desc.resource_layout_token == 0) {
        fail("resource set references an invalid pipeline layout token");
    }
    std::vector<wgpu::BindGroupEntry> entries;
    for (uint32_t group_index = 0; group_index < desc.group_count; ++group_index) {
        const BoundResourceGroupView& group = desc.groups[group_index];
        for (uint32_t binding_index = 0; binding_index < group.binding_count; ++binding_index) {
            const BoundResourceBinding& binding = group.bindings[binding_index];
            if (binding.slot.placement.kind != BackendPlacementKind::WebGPU ||
                binding.slot.placement.webgpu.group != 0) {
                fail(std::string("resource '") + bound_resource_debug_name(binding) +
                     "' has no WebGPU group-0 placement");
            }
            const auto layout_item = std::find_if(
                pipeline->layout.begin(), pipeline->layout.end(),
                [&](const WebGpuLayoutEntry& item) {
                    return item.binding == binding.slot.placement.webgpu.binding;
                });
            if (layout_item == pipeline->layout.end() ||
                layout_item->kind != binding.slot.kind) {
                fail(std::string("resource '") + bound_resource_debug_name(binding) +
                     "' does not match the pipeline layout");
            }
            const bool value_kind_matches =
                (layout_item->kind == ShaderResourceKind::ConstantBuffer &&
                 binding.value.kind == BoundResourceKind::UniformBuffer) ||
                (layout_item->kind == ShaderResourceKind::StorageBuffer &&
                 binding.value.kind == BoundResourceKind::StorageBuffer) ||
                (layout_item->kind == ShaderResourceKind::Texture &&
                 binding.value.kind == BoundResourceKind::SampledTexture) ||
                (layout_item->kind == ShaderResourceKind::Sampler &&
                 binding.value.kind == BoundResourceKind::Sampler);
            if (!value_kind_matches) {
                fail(std::string("resource '") + bound_resource_debug_name(binding) +
                     "' value kind does not match its shader contract");
            }
            wgpu::BindGroupEntry native;
            native.binding = binding.slot.placement.webgpu.binding;
            switch (binding.value.kind) {
                case BoundResourceKind::UniformBuffer:
                case BoundResourceKind::StorageBuffer: {
                    const WebGpuBuffer* buffer = buffers_.get(binding.value.buffer.id);
                    if (!buffer) fail("resource set references an invalid buffer");
                    native.buffer = buffer->object;
                    native.offset = binding.value.offset;
                    native.size = binding.value.range == 0
                        ? buffer->desc.size - binding.value.offset : binding.value.range;
                    entries.push_back(native);
                    break;
                }
                case BoundResourceKind::SampledTexture: {
                    const WebGpuTexture* texture = textures_.get(binding.value.texture.id);
                    const SamplerHandle sampler_handle = binding.value.sampler
                        ? binding.value.sampler : default_sampler_;
                    const WebGpuSampler* sampler = samplers_.get(sampler_handle.id);
                    if (!texture || !sampler) fail("sampled texture requires valid texture and sampler");
                    native.textureView = texture->view;
                    entries.push_back(native);
                    if (!binding.slot.placement.webgpu.has_sampler_binding) {
                        fail("sampled texture placement has no sampler_binding");
                    }
                    wgpu::BindGroupEntry sampler_entry;
                    sampler_entry.binding = binding.slot.placement.webgpu.sampler_binding;
                    sampler_entry.sampler = sampler->object;
                    entries.push_back(sampler_entry);
                    break;
                }
                case BoundResourceKind::Sampler: {
                    const SamplerHandle sampler_handle = binding.value.sampler
                        ? binding.value.sampler : default_sampler_;
                    const WebGpuSampler* sampler = samplers_.get(sampler_handle.id);
                    if (!sampler) fail("resource set references an invalid sampler");
                    native.sampler = sampler->object;
                    entries.push_back(native);
                    break;
                }
            }
        }
    }
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) { return a.binding < b.binding; });
    size_t expected_entry_count = 0;
    for (const WebGpuLayoutEntry& item : pipeline->layout) {
        expected_entry_count += item.has_sampler_binding ? 2u : 1u;
    }
    if (entries.size() != expected_entry_count) {
        fail("resource set does not provide every pipeline binding exactly once");
    }
    for (size_t index = 1; index < entries.size(); ++index) {
        if (entries[index - 1].binding == entries[index].binding) {
            fail("resource set provides the same WebGPU binding more than once");
        }
    }
    wgpu::BindGroupDescriptor native;
    native.layout = pipeline->bind_group_layout;
    native.entryCount = entries.size();
    native.entries = entries.data();
    wgpu::BindGroup object = device_.CreateBindGroup(&native);
    if (!object) fail("CreateBindGroup failed");
    return {resource_sets_.add({std::move(object), desc.resource_layout_token})};
}

void WebGpuRenderDevice::destroy(BufferHandle handle) { buffers_.remove(handle.id); }
void WebGpuRenderDevice::destroy(TextureHandle handle) {
    if (handle == acquired_surface_texture_) acquired_surface_texture_ = {};
    textures_.remove(handle.id);
}
void WebGpuRenderDevice::destroy(SamplerHandle handle) { samplers_.remove(handle.id); }
void WebGpuRenderDevice::destroy(ShaderHandle handle) { shaders_.remove(handle.id); }
void WebGpuRenderDevice::destroy(PipelineHandle handle) { pipelines_.remove(handle.id); }
void WebGpuRenderDevice::destroy(ResourceSetHandle handle) { resource_sets_.remove(handle.id); }

void WebGpuRenderDevice::upload_buffer(
    BufferHandle dst, std::span<const uint8_t> data, uint64_t offset) {
    const WebGpuBuffer* buffer = buffers_.get(dst.id);
    if (!buffer || offset + data.size() > buffer->desc.size) fail("buffer upload is out of bounds");
    queue_.WriteBuffer(buffer->object, offset, data.data(), data.size());
}

void WebGpuRenderDevice::upload_texture(
    TextureHandle dst, std::span<const uint8_t> data, uint32_t mip) {
    const WebGpuTexture* texture = textures_.get(dst.id);
    if (!texture) fail("texture upload references an invalid texture");
    const uint32_t width = std::max(1u, texture->desc.width >> mip);
    const uint32_t height = std::max(1u, texture->desc.height >> mip);
    upload_texture_region(dst, 0, 0, width, height, data, mip);
}

void WebGpuRenderDevice::upload_texture_region(
    TextureHandle dst, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
    std::span<const uint8_t> data, uint32_t mip) {
    const WebGpuTexture* texture = textures_.get(dst.id);
    if (!texture || mip >= texture->desc.mip_levels) fail("invalid texture upload target");
    const uint32_t mip_width = std::max(1u, texture->desc.width >> mip);
    const uint32_t mip_height = std::max(1u, texture->desc.height >> mip);
    const uint32_t stride = width * bytes_per_pixel(texture->desc.format);
    if (x + width > mip_width || y + height > mip_height ||
        data.size() != static_cast<size_t>(stride) * height) {
        fail("texture upload region or byte count is invalid");
    }
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = texture->object;
    destination.mipLevel = mip;
    destination.origin = {x, y, 0};
    wgpu::TexelCopyBufferLayout layout;
    layout.bytesPerRow = stride;
    layout.rowsPerImage = height;
    wgpu::Extent3D extent{width, height, 1};
    queue_.WriteTexture(&destination, data.data(), data.size(), &layout, &extent);
}

void WebGpuRenderDevice::read_buffer(
    BufferHandle, std::span<uint8_t>, uint64_t) {
    fail("synchronous buffer readback is unsupported by WebGPU; use an async API");
}

uintptr_t WebGpuRenderDevice::pipeline_resource_layout_token(
    PipelineHandle handle) const {
    const WebGpuPipeline* pipeline = pipelines_.get(handle.id);
    return pipeline ? pipeline->layout_token : 0;
}

TextureDesc WebGpuRenderDevice::texture_desc(TextureHandle handle) const {
    const WebGpuTexture* texture = textures_.get(handle.id);
    return texture ? texture->desc : TextureDesc{};
}

std::unique_ptr<ICommandList> WebGpuRenderDevice::create_command_list(QueueType queue) {
    if (queue != QueueType::Graphics) fail("only the WebGPU graphics queue is exposed");
    return std::make_unique<WebGpuCommandList>(*this);
}

void WebGpuRenderDevice::submit(ICommandList& command_list) {
    auto* webgpu = dynamic_cast<WebGpuCommandList*>(&command_list);
    if (!webgpu) fail("command list belongs to another backend");
    wgpu::CommandBuffer command = webgpu->take_command_buffer();
    if (!command) fail("command list was not ended before submit");
    queue_.Submit(1, &command);
}

void WebGpuRenderDevice::present() {
    if (!acquired_surface_texture_) fail("present called without an acquired surface texture");
#if !defined(__EMSCRIPTEN__)
    surface_.Present();
    textures_.remove(acquired_surface_texture_.id);
    acquired_surface_texture_ = {};
#endif
    // Browser WebGPU presents at the end of requestAnimationFrame and
    // wgpuSurfacePresent deliberately aborts. Retain the current texture
    // through that boundary; acquire_surface_texture releases it at the
    // beginning of the next frame.
}

void WebGpuRenderDevice::configure_surface(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) fail("surface extent must be non-zero");
    if (acquired_surface_texture_) {
        textures_.remove(acquired_surface_texture_.id);
        acquired_surface_texture_ = {};
    }
    wgpu::SurfaceConfiguration config;
    config.device = device_;
    config.format = surface_format_;
    config.usage = wgpu::TextureUsage::RenderAttachment |
                   wgpu::TextureUsage::CopyDst;
    config.width = width;
    config.height = height;
    config.presentMode = wgpu::PresentMode::Fifo;
    config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
    surface_.Configure(&config);
    surface_width_ = width;
    surface_height_ = height;
}

TextureHandle WebGpuRenderDevice::acquire_surface_texture() {
    if (acquired_surface_texture_) {
        textures_.remove(acquired_surface_texture_.id);
        acquired_surface_texture_ = {};
    }
    wgpu::SurfaceTexture surface_texture;
    surface_.GetCurrentTexture(&surface_texture);
    if (surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
        fail("GetCurrentTexture failed with status " +
             std::to_string(static_cast<unsigned>(surface_texture.status)));
    }
    TextureDesc desc;
    desc.width = surface_width_;
    desc.height = surface_height_;
    desc.format = surface_pixel_format();
    desc.usage = TextureUsage::ColorAttachment | TextureUsage::CopyDst;
    wgpu::TextureView view = surface_texture.texture.CreateView();
    acquired_surface_texture_ = {textures_.add(
        {std::move(surface_texture.texture), std::move(view), desc, true})};
    return acquired_surface_texture_;
}

PixelFormat WebGpuRenderDevice::surface_pixel_format() const {
    if (surface_format_ == wgpu::TextureFormat::BGRA8Unorm) return PixelFormat::BGRA8_UNorm;
    if (surface_format_ == wgpu::TextureFormat::RGBA8Unorm) return PixelFormat::RGBA8_UNorm;
    if (surface_format_ == wgpu::TextureFormat::BGRA8UnormSrgb) return PixelFormat::BGRA8_sRGB;
    if (surface_format_ == wgpu::TextureFormat::RGBA8UnormSrgb) return PixelFormat::RGBA8_sRGB;
    fail("canvas selected an unsupported surface format");
}

} // namespace tgfx
