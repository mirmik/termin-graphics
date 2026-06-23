#pragma once

#include "tgfx2/d3d11/d3d11_render_device.hpp"

#include <string>
#include <vector>

#include <d3d11.h>
#include <d3dcommon.h>

namespace tgfx::d3d11_internal {

struct D3D11InputSemantic {
    std::string name;
    UINT index = 0;
};

struct D3D11SignatureParam {
    std::string semantic;
    UINT semantic_index = 0;
    UINT register_index = 0;
    BYTE mask = 0;
    D3D_NAME system_value = D3D_NAME_UNDEFINED;
};

D3D11InputSemantic semantic_for_attribute(const VertexAttribute& attr);
std::vector<D3D11InputSemantic> reflect_d3d11_vertex_inputs(const D3D11ShaderModule& vs);
std::vector<D3D11SignatureParam> reflect_d3d11_signature(
    const D3D11ShaderModule& shader,
    bool output_signature);
bool signatures_have_link_mismatch(
    const std::vector<D3D11SignatureParam>& vs_outputs,
    const std::vector<D3D11SignatureParam>& ps_inputs);
void log_d3d11_shader_signatures(
    const char* reason,
    const D3D11ShaderModule& vs,
    const D3D11ShaderModule& ps,
    const std::vector<D3D11SignatureParam>& vs_outputs,
    const std::vector<D3D11SignatureParam>& ps_inputs);
void log_d3d11_input_layout_failure(
    HRESULT hr,
    const std::vector<D3D11_INPUT_ELEMENT_DESC>& input_elements,
    const std::vector<D3D11InputSemantic>& reflected_inputs);

} // namespace tgfx::d3d11_internal
