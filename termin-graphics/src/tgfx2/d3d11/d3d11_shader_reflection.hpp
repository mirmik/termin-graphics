#pragma once

#include "tgfx2/d3d11/d3d11_render_device.hpp"

#include <string>
#include <string_view>
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

    // Slang-generated DXBC may encode a semantic such as TEXCOORD1 as the
    // literal name "TEXCOORD1" with SemanticIndex == 0. D3DCompile normally
    // reflects the same declaration as name "TEXCOORD" and index 1. Normalize
    // both representations before constructing an input layout or comparing
    // stage signatures.
    inline D3D11InputSemantic normalize_reflected_semantic(std::string_view name, UINT index) {
        std::string normalized(name);
        for (char& ch : normalized) {
            if (ch >= 'a' && ch <= 'z') {
                ch = static_cast<char>(ch - 'a' + 'A');
            }
        }
        if (index != 0) {
            return {std::move(normalized), index};
        }

        size_t suffix_start = normalized.size();
        while (suffix_start > 0 && normalized[suffix_start - 1] >= '0' && normalized[suffix_start - 1] <= '9') {
            --suffix_start;
        }
        if (suffix_start == normalized.size()) {
            return {std::move(normalized), 0};
        }

        UINT suffix_index = 0;
        for (size_t i = suffix_start; i < normalized.size(); ++i) {
            suffix_index = suffix_index * 10u + static_cast<UINT>(normalized[i] - '0');
        }
        normalized.resize(suffix_start);
        return {std::move(normalized), suffix_index};
    }

    D3D11InputSemantic semantic_for_attribute(const VertexAttributeDesc& attr);
    std::vector<D3D11InputSemantic> reflect_d3d11_vertex_inputs(const D3D11ShaderModule& vs);
    std::vector<D3D11SignatureParam> reflect_d3d11_signature(const D3D11ShaderModule& shader, bool output_signature);
    bool signatures_have_link_mismatch(const std::vector<D3D11SignatureParam>& vs_outputs,
                                       const std::vector<D3D11SignatureParam>& ps_inputs);
    void log_d3d11_shader_signatures(const char* reason,
                                     const D3D11ShaderModule& vs,
                                     const D3D11ShaderModule& ps,
                                     const std::vector<D3D11SignatureParam>& vs_outputs,
                                     const std::vector<D3D11SignatureParam>& ps_inputs);
    void log_d3d11_input_layout_failure(HRESULT hr,
                                        const std::vector<D3D11_INPUT_ELEMENT_DESC>& input_elements,
                                        const std::vector<D3D11InputSemantic>& reflected_inputs);

} // namespace tgfx::d3d11_internal
