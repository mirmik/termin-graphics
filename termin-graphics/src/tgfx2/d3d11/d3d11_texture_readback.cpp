#include "tgfx2/d3d11/d3d11_render_device.hpp"

#include "tgfx2/d3d11/d3d11_type_conversions.hpp"
#include "tgfx2/pixel_format_utils.hpp"

#include <cstdint>
#include <cstring>

#include <tcbase/tc_log.hpp>

namespace tgfx {
    namespace {
        float half_to_float(uint16_t h) {
            const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16u;
            uint32_t exp = (h >> 10u) & 0x1fu;
            uint32_t mant = h & 0x03ffu;

            uint32_t bits = 0;
            if (exp == 0) {
                if (mant == 0) {
                    bits = sign;
                } else {
                    exp = 1;
                    while ((mant & 0x0400u) == 0) {
                        mant <<= 1u;
                        --exp;
                    }
                    mant &= 0x03ffu;
                    bits = sign | ((exp + 112u) << 23u) | (mant << 13u);
                }
            } else if (exp == 31) {
                bits = sign | 0x7f800000u | (mant << 13u);
            } else {
                bits = sign | ((exp + 112u) << 23u) | (mant << 13u);
            }

            float out = 0.0f;
            std::memcpy(&out, &bits, sizeof(out));
            return out;
        }

        bool unpack_rgba_float_pixel(PixelFormat format, const uint8_t* src, float* dst) {
            switch (format) {
            case PixelFormat::R8_UNorm:
                dst[0] = src[0] / 255.0f;
                dst[1] = 0.0f;
                dst[2] = 0.0f;
                dst[3] = 1.0f;
                return true;
            case PixelFormat::RG8_UNorm:
                dst[0] = src[0] / 255.0f;
                dst[1] = src[1] / 255.0f;
                dst[2] = 0.0f;
                dst[3] = 1.0f;
                return true;
            case PixelFormat::RGB8_UNorm:
                dst[0] = src[0] / 255.0f;
                dst[1] = src[1] / 255.0f;
                dst[2] = src[2] / 255.0f;
                dst[3] = 1.0f;
                return true;
            case PixelFormat::RGBA8_UNorm:
            case PixelFormat::RGBA8_sRGB:
                dst[0] = src[0] / 255.0f;
                dst[1] = src[1] / 255.0f;
                dst[2] = src[2] / 255.0f;
                dst[3] = src[3] / 255.0f;
                return true;
            case PixelFormat::BGRA8_UNorm:
            case PixelFormat::BGRA8_sRGB:
                dst[0] = src[2] / 255.0f;
                dst[1] = src[1] / 255.0f;
                dst[2] = src[0] / 255.0f;
                dst[3] = src[3] / 255.0f;
                return true;
            case PixelFormat::R16F: {
                uint16_t r = 0;
                std::memcpy(&r, src, sizeof(r));
                dst[0] = half_to_float(r);
                dst[1] = 0.0f;
                dst[2] = 0.0f;
                dst[3] = 1.0f;
                return true;
            }
            case PixelFormat::RG16F: {
                uint16_t rg[2] = {};
                std::memcpy(rg, src, sizeof(rg));
                dst[0] = half_to_float(rg[0]);
                dst[1] = half_to_float(rg[1]);
                dst[2] = 0.0f;
                dst[3] = 1.0f;
                return true;
            }
            case PixelFormat::RGBA16F: {
                uint16_t rgba[4] = {};
                std::memcpy(rgba, src, sizeof(rgba));
                dst[0] = half_to_float(rgba[0]);
                dst[1] = half_to_float(rgba[1]);
                dst[2] = half_to_float(rgba[2]);
                dst[3] = half_to_float(rgba[3]);
                return true;
            }
            case PixelFormat::R32F:
                std::memcpy(&dst[0], src, sizeof(float));
                dst[1] = 0.0f;
                dst[2] = 0.0f;
                dst[3] = 1.0f;
                return true;
            case PixelFormat::RG32F:
                std::memcpy(dst, src, sizeof(float) * 2u);
                dst[2] = 0.0f;
                dst[3] = 1.0f;
                return true;
            case PixelFormat::RGBA32F:
                std::memcpy(dst, src, sizeof(float) * 4u);
                return true;
            default:
                return false;
            }
        }

        bool supports_rgba_float_readback(PixelFormat format) {
            switch (format) {
            case PixelFormat::R8_UNorm:
            case PixelFormat::RG8_UNorm:
            case PixelFormat::RGB8_UNorm:
            case PixelFormat::RGBA8_UNorm:
            case PixelFormat::BGRA8_UNorm:
            case PixelFormat::RGBA8_sRGB:
            case PixelFormat::BGRA8_sRGB:
            case PixelFormat::R16F:
            case PixelFormat::RG16F:
            case PixelFormat::RGBA16F:
            case PixelFormat::R32F:
            case PixelFormat::RG32F:
            case PixelFormat::RGBA32F:
                return true;
            default:
                return false;
            }
        }
    } // namespace

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
            tc::Log::error("D3D11RenderDevice::create_staging_texture failed: HRESULT=0x%08X",
                           static_cast<unsigned>(hr));
        }
        return staging;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> D3D11RenderDevice::resolve_texture_for_readback(const D3D11Texture& src) {
        if (!src.texture) {
            return {};
        }
        if (src.desc.sample_count <= 1) {
            return src.texture;
        }

        if (d3d11::is_depth_format(src.desc.format)) {
            tc::Log::error("D3D11RenderDevice::resolve_texture_for_readback: MSAA depth readback is not supported");
            return {};
        }

        D3D11_TEXTURE2D_DESC desc{};
        src.texture->GetDesc(&desc);
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> resolved;
        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &resolved);
        if (FAILED(hr) || !resolved) {
            tc::Log::error(
                "D3D11RenderDevice::resolve_texture_for_readback: resolve texture creation failed: HRESULT=0x%08X",
                static_cast<unsigned>(hr));
            return {};
        }

        context_->ResolveSubresource(resolved.Get(), 0, src.texture.Get(), 0, d3d11::to_dxgi_format(src.desc.format));
        return resolved;
    }

    bool D3D11RenderDevice::read_pixel_rgba8(TextureHandle handle, int x, int y, float out_rgba[4]) {
        if (!out_rgba)
            return false;
        auto* tex = get_texture(handle);
        if (!tex || !tex->texture)
            return false;
        if (!is_rgba8_family(tex->desc.format)) {
            tc::Log::error("D3D11RenderDevice::read_pixel_rgba8: unsupported format");
            return false;
        }
        if (x < 0 || y < 0 || x >= static_cast<int>(tex->desc.width) || y >= static_cast<int>(tex->desc.height)) {
            return false;
        }

        D3D11Texture read_src = *tex;
        read_src.texture = resolve_texture_for_readback(*tex);
        read_src.desc.sample_count = 1;
        if (!read_src.texture)
            return false;

        auto staging = create_staging_texture(read_src);
        if (!staging)
            return false;
        context_->CopyResource(staging.Get(), read_src.texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            tc::Log::error("D3D11RenderDevice::read_pixel_rgba8 Map failed: HRESULT=0x%08X", static_cast<unsigned>(hr));
            return false;
        }
        const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        const auto* p = row + static_cast<size_t>(x) * 4u;
        if (tex->desc.format == PixelFormat::BGRA8_UNorm || tex->desc.format == PixelFormat::BGRA8_sRGB) {
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

    uint64_t D3D11RenderDevice::request_pixel_rgba8(TextureHandle handle, int x, int y) {
        return request_pixel_readback(handle, x, y, PixelReadbackKind::Rgba8);
    }

    bool D3D11RenderDevice::poll_pixel_rgba8(uint64_t request_id, float out_rgba[4]) {
        if (request_id == 0 || !out_rgba)
            return false;
        PixelReadbackSlot* slot = find_pixel_readback_slot(request_id);
        if (!slot || !pixel_readback_ready(*slot, true))
            return false;
        if (slot->kind != PixelReadbackKind::Rgba8) {
            tc::Log::error("D3D11RenderDevice::poll_pixel_rgba8: request %llu has the wrong kind",
                           static_cast<unsigned long long>(request_id));
            release_pixel_readback_slot(*slot);
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context_->Map(slot->staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
            return false;
        if (FAILED(hr) || !mapped.pData) {
            tc::Log::error("D3D11RenderDevice::poll_pixel_rgba8: Map failed for request %llu: HRESULT=0x%08X",
                           static_cast<unsigned long long>(request_id),
                           static_cast<unsigned>(hr));
            release_pixel_readback_slot(*slot);
            return false;
        }

        const auto* pixel = static_cast<const uint8_t*>(mapped.pData);
        if (slot->format == PixelFormat::BGRA8_UNorm || slot->format == PixelFormat::BGRA8_sRGB) {
            out_rgba[0] = pixel[2] / 255.0f;
            out_rgba[1] = pixel[1] / 255.0f;
            out_rgba[2] = pixel[0] / 255.0f;
            out_rgba[3] = pixel[3] / 255.0f;
        } else {
            out_rgba[0] = pixel[0] / 255.0f;
            out_rgba[1] = pixel[1] / 255.0f;
            out_rgba[2] = pixel[2] / 255.0f;
            out_rgba[3] = pixel[3] / 255.0f;
        }
        context_->Unmap(slot->staging.Get(), 0);
        release_pixel_readback_slot(*slot);
        return true;
    }

    uint64_t D3D11RenderDevice::request_pixel_depth_float(TextureHandle handle, int x, int y) {
        return request_pixel_readback(handle, x, y, PixelReadbackKind::DepthF32);
    }

    bool D3D11RenderDevice::poll_pixel_depth_float(uint64_t request_id, float* out_depth) {
        if (request_id == 0 || !out_depth)
            return false;
        PixelReadbackSlot* slot = find_pixel_readback_slot(request_id);
        if (!slot || !pixel_readback_ready(*slot, true))
            return false;
        if (slot->kind != PixelReadbackKind::DepthF32) {
            tc::Log::error("D3D11RenderDevice::poll_pixel_depth_float: request %llu has the wrong kind",
                           static_cast<unsigned long long>(request_id));
            release_pixel_readback_slot(*slot);
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context_->Map(slot->staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
            return false;
        if (FAILED(hr) || !mapped.pData) {
            tc::Log::error("D3D11RenderDevice::poll_pixel_depth_float: Map failed for request %llu: HRESULT=0x%08X",
                           static_cast<unsigned long long>(request_id),
                           static_cast<unsigned>(hr));
            release_pixel_readback_slot(*slot);
            return false;
        }

        std::memcpy(out_depth, mapped.pData, sizeof(float));
        context_->Unmap(slot->staging.Get(), 0);
        release_pixel_readback_slot(*slot);
        return true;
    }

    uint64_t D3D11RenderDevice::request_pixel_readback(TextureHandle handle, int x, int y, PixelReadbackKind kind) {
        D3D11Texture* texture = get_texture(handle);
        if (!texture || !texture->texture)
            return 0;
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= texture->desc.width ||
            static_cast<uint32_t>(y) >= texture->desc.height) {
            tc::Log::error(
                "D3D11RenderDevice::request_pixel_readback: coordinates (%d,%d) are outside texture %u (%ux%u)",
                x,
                y,
                handle.id,
                texture->desc.width,
                texture->desc.height);
            return 0;
        }
        if (texture->desc.sample_count != 1) {
            tc::Log::error("D3D11RenderDevice::request_pixel_readback: multisampled texture %u is unsupported",
                           handle.id);
            return 0;
        }
        if (kind == PixelReadbackKind::Rgba8 && !is_rgba8_family(texture->desc.format)) {
            tc::Log::error("D3D11RenderDevice::request_pixel_readback: texture %u is not RGBA8/BGRA8", handle.id);
            return 0;
        }
        if (kind == PixelReadbackKind::DepthF32 && texture->desc.format != PixelFormat::D32F) {
            tc::Log::error("D3D11RenderDevice::request_pixel_readback: texture %u is not D32F", handle.id);
            return 0;
        }
        if (kind == PixelReadbackKind::DepthF32 && !texture->srv) {
            tc::Log::error("D3D11RenderDevice::request_pixel_readback: depth texture %u has no sampled SRV", handle.id);
            return 0;
        }

        PixelReadbackSlot* slot = acquire_pixel_readback_slot(kind, texture->desc.format);
        if (!slot) {
            tc::Log::error("D3D11RenderDevice::request_pixel_readback: all %zu slots are pending",
                           kPixelReadbackSlotCount);
            return 0;
        }

        D3D11Texture* copy_source = texture;
        int copy_x = x;
        int copy_y = y;
        if (kind == PixelReadbackKind::DepthF32) {
            if (!pixel_readback_depth_target_) {
                TextureDesc depth_target_desc;
                depth_target_desc.width = 1;
                depth_target_desc.height = 1;
                depth_target_desc.format = PixelFormat::R32F;
                depth_target_desc.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
                pixel_readback_depth_target_ = create_texture(depth_target_desc);
                if (!pixel_readback_depth_target_) {
                    tc::Log::error(
                        "D3D11RenderDevice::request_pixel_readback: depth conversion target creation failed");
                    release_pixel_readback_slot(*slot);
                    return 0;
                }
            }
            blit_to_texture(pixel_readback_depth_target_,
                            handle,
                            termin::Bounds2i{x, y, x + 1, y + 1},
                            termin::Bounds2i::from_size(1, 1));
            copy_source = get_texture(pixel_readback_depth_target_);
            copy_x = 0;
            copy_y = 0;
            if (!copy_source || !copy_source->texture) {
                tc::Log::error("D3D11RenderDevice::request_pixel_readback: depth conversion target is invalid");
                release_pixel_readback_slot(*slot);
                return 0;
            }
        }

        D3D11_TEXTURE2D_DESC source_desc{};
        copy_source->texture->GetDesc(&source_desc);
        D3D11_TEXTURE2D_DESC staging_desc = source_desc;
        staging_desc.Width = 1;
        staging_desc.Height = 1;
        staging_desc.MipLevels = 1;
        staging_desc.ArraySize = 1;
        staging_desc.SampleDesc.Count = 1;
        staging_desc.SampleDesc.Quality = 0;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;

        bool create_staging = !slot->staging;
        if (slot->staging) {
            D3D11_TEXTURE2D_DESC existing_desc{};
            slot->staging->GetDesc(&existing_desc);
            create_staging = existing_desc.Format != staging_desc.Format;
        }
        if (create_staging) {
            slot->staging.Reset();
            const HRESULT hr = device_->CreateTexture2D(&staging_desc, nullptr, &slot->staging);
            if (FAILED(hr) || !slot->staging) {
                tc::Log::error("D3D11RenderDevice::request_pixel_readback: 1x1 staging creation failed: HRESULT=0x%08X",
                               static_cast<unsigned>(hr));
                release_pixel_readback_slot(*slot);
                return 0;
            }
        }
        if (!slot->completion) {
            D3D11_QUERY_DESC query_desc{};
            query_desc.Query = D3D11_QUERY_EVENT;
            const HRESULT hr = device_->CreateQuery(&query_desc, &slot->completion);
            if (FAILED(hr) || !slot->completion) {
                tc::Log::error("D3D11RenderDevice::request_pixel_readback: event query creation failed: HRESULT=0x%08X",
                               static_cast<unsigned>(hr));
                release_pixel_readback_slot(*slot);
                return 0;
            }
        }

        const D3D11_BOX source_box{
            static_cast<UINT>(copy_x),
            static_cast<UINT>(copy_y),
            0,
            static_cast<UINT>(copy_x + 1),
            static_cast<UINT>(copy_y + 1),
            1,
        };
        context_->CopySubresourceRegion(slot->staging.Get(), 0, 0, 0, 0, copy_source->texture.Get(), 0, &source_box);
        context_->End(slot->completion.Get());

        slot->request_id = next_pixel_readback_id_++;
        if (slot->request_id == 0)
            slot->request_id = next_pixel_readback_id_++;
        slot->issue_sequence = pixel_readback_issue_sequence_++;
        slot->kind = kind;
        slot->format = texture->desc.format;
        slot->active = true;
        return slot->request_id;
    }

    D3D11RenderDevice::PixelReadbackSlot* D3D11RenderDevice::acquire_pixel_readback_slot(PixelReadbackKind kind,
                                                                                         PixelFormat format) {
        (void)kind;
        (void)format;
        for (PixelReadbackSlot& slot : pixel_readback_slots_) {
            if (!slot.active)
                return &slot;
        }

        PixelReadbackSlot* oldest_completed = nullptr;
        for (PixelReadbackSlot& slot : pixel_readback_slots_) {
            if (pixel_readback_ready(slot, true) &&
                (!oldest_completed || slot.issue_sequence < oldest_completed->issue_sequence)) {
                oldest_completed = &slot;
            }
        }
        if (!oldest_completed)
            return nullptr;

        tc::Log::warn("D3D11RenderDevice::request_pixel_readback: reclaiming unpolled completed request %llu",
                      static_cast<unsigned long long>(oldest_completed->request_id));
        release_pixel_readback_slot(*oldest_completed);
        return oldest_completed;
    }

    D3D11RenderDevice::PixelReadbackSlot* D3D11RenderDevice::find_pixel_readback_slot(uint64_t request_id) {
        for (PixelReadbackSlot& slot : pixel_readback_slots_) {
            if (slot.active && slot.request_id == request_id)
                return &slot;
        }
        return nullptr;
    }

    bool D3D11RenderDevice::pixel_readback_ready(PixelReadbackSlot& slot, bool log_failure) {
        if (!slot.active || !slot.completion)
            return false;
        // GetData is non-blocking for an event query. Allow it to flush queued
        // commands so offscreen hosts that never call present() still make
        // progress; DONOTFLUSH can leave a low-traffic hover request pending
        // indefinitely.
        const HRESULT hr = context_->GetData(slot.completion.Get(), nullptr, 0, 0);
        if (hr == S_OK)
            return true;
        if (hr == S_FALSE)
            return false;
        if (log_failure) {
            tc::Log::error("D3D11RenderDevice::pixel_readback_ready: GetData failed for request %llu: HRESULT=0x%08X",
                           static_cast<unsigned long long>(slot.request_id),
                           static_cast<unsigned>(hr));
        }
        release_pixel_readback_slot(slot);
        return false;
    }

    void D3D11RenderDevice::release_pixel_readback_slot(PixelReadbackSlot& slot) {
        slot.active = false;
        slot.request_id = 0;
        slot.issue_sequence = 0;
    }

    bool D3D11RenderDevice::read_texture_rgba_float(TextureHandle handle, float* out) {
        auto* tex = get_texture(handle);
        if (!tex || !tex->texture || !out)
            return false;
        if (d3d11::is_depth_format(tex->desc.format)) {
            tc::Log::error("D3D11RenderDevice::read_texture_rgba_float: texture is a depth format");
            return false;
        }
        const uint32_t bytes_per_pixel = d3d11::pixel_format_bytes(tex->desc.format);
        if (bytes_per_pixel == 0 || !supports_rgba_float_readback(tex->desc.format)) {
            tc::Log::error("D3D11RenderDevice::read_texture_rgba_float: unsupported format");
            return false;
        }

        D3D11Texture read_src = *tex;
        read_src.texture = resolve_texture_for_readback(*tex);
        read_src.desc.sample_count = 1;
        if (!read_src.texture)
            return false;

        auto staging = create_staging_texture(read_src);
        if (!staging)
            return false;
        context_->CopyResource(staging.Get(), read_src.texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            tc::Log::error("D3D11RenderDevice::read_texture_rgba_float Map failed: "
                           "HRESULT=0x%08X device_removed_reason=0x%08X",
                           static_cast<unsigned>(hr),
                           static_cast<unsigned>(device_->GetDeviceRemovedReason()));
            return false;
        }
        for (uint32_t y = 0; y < tex->desc.height; ++y) {
            const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
            for (uint32_t x = 0; x < tex->desc.width; ++x) {
                const auto* p = row + static_cast<size_t>(x) * bytes_per_pixel;
                float* dst = out + (static_cast<size_t>(y) * tex->desc.width + x) * 4u;
                unpack_rgba_float_pixel(tex->desc.format, p, dst);
            }
        }
        context_->Unmap(staging.Get(), 0);
        return true;
    }

    bool D3D11RenderDevice::read_texture_depth_float(TextureHandle handle, float* out) {
        auto* tex = get_texture(handle);
        if (!tex || !tex->texture || !out)
            return false;
        if (tex->desc.format != PixelFormat::D32F) {
            tc::Log::error("D3D11RenderDevice::read_texture_depth_float: unsupported format");
            return false;
        }
        if (tex->desc.sample_count > 1) {
            tc::Log::error("D3D11RenderDevice::read_texture_depth_float: MSAA depth readback is not supported");
            return false;
        }

        auto staging = create_staging_texture(*tex);
        if (!staging)
            return false;
        context_->CopyResource(staging.Get(), tex->texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            tc::Log::error("D3D11RenderDevice::read_texture_depth_float Map failed: HRESULT=0x%08X",
                           static_cast<unsigned>(hr));
            return false;
        }
        for (uint32_t y = 0; y < tex->desc.height; ++y) {
            const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
            std::memcpy(out + static_cast<size_t>(y) * tex->desc.width,
                        row,
                        static_cast<size_t>(tex->desc.width) * sizeof(float));
        }
        context_->Unmap(staging.Get(), 0);
        return true;
    }

    bool D3D11RenderDevice::read_pixel_depth_float(TextureHandle handle, int x, int y, float* out_depth) {
        auto* tex = get_texture(handle);
        if (!tex || !tex->texture || !out_depth)
            return false;
        if (tex->desc.format != PixelFormat::D32F) {
            tc::Log::error("D3D11RenderDevice::read_pixel_depth_float: unsupported format");
            return false;
        }
        if (tex->desc.sample_count > 1) {
            tc::Log::error("D3D11RenderDevice::read_pixel_depth_float: MSAA depth readback is not supported");
            return false;
        }
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= tex->desc.width ||
            static_cast<uint32_t>(y) >= tex->desc.height) {
            tc::Log::error("D3D11RenderDevice::read_pixel_depth_float: coordinates out of bounds");
            return false;
        }

        auto staging = create_staging_texture(*tex);
        if (!staging)
            return false;
        context_->CopyResource(staging.Get(), tex->texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            tc::Log::error("D3D11RenderDevice::read_pixel_depth_float Map failed: HRESULT=0x%08X",
                           static_cast<unsigned>(hr));
            return false;
        }

        const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        std::memcpy(out_depth, row + static_cast<size_t>(x) * sizeof(float), sizeof(float));
        context_->Unmap(staging.Get(), 0);
        return true;
    }

} // namespace tgfx
