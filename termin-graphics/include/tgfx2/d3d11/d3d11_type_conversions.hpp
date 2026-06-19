#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d11.h>
#include <dxgiformat.h>

#include "tgfx2/descriptors.hpp"

namespace tgfx::d3d11 {

DXGI_FORMAT to_dxgi_format(PixelFormat format);
DXGI_FORMAT to_dxgi_srv_format(PixelFormat format);
DXGI_FORMAT to_dxgi_dsv_format(PixelFormat format);
DXGI_FORMAT to_dxgi_vertex_format(VertexFormat format);
DXGI_FORMAT to_dxgi_index_format(IndexType type);
D3D11_PRIMITIVE_TOPOLOGY to_d3d_topology(PrimitiveTopology topology);
D3D11_COMPARISON_FUNC to_d3d_compare(CompareOp op);
D3D11_CULL_MODE to_d3d_cull(CullMode mode);
D3D11_FILL_MODE to_d3d_fill(PolygonMode mode);
D3D11_BLEND to_d3d_blend_factor(BlendFactor factor);
D3D11_BLEND_OP to_d3d_blend_op(BlendOp op);
D3D11_FILTER to_d3d_filter(const SamplerDesc& desc);
D3D11_TEXTURE_ADDRESS_MODE to_d3d_address(AddressMode mode);
uint32_t pixel_format_bytes(PixelFormat format);
bool is_depth_format(PixelFormat format);

} // namespace tgfx::d3d11
