#include "d3d11_shader_reflection.hpp"

#include "tgfx2/tc_mesh_bridge.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include <d3dcompiler.h>
#include <tcbase/tc_log.hpp>

namespace tgfx::d3d11_internal {

static std::string uppercase_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

static D3D11InputSemantic parse_d3d11_semantic(std::string semantic) {
    if (semantic.empty()) {
        return {"TEXCOORD", 0};
    }

    semantic = uppercase_ascii(std::move(semantic));
    size_t suffix_start = semantic.size();
    while (suffix_start > 0 &&
           std::isdigit(static_cast<unsigned char>(semantic[suffix_start - 1]))) {
        --suffix_start;
    }
    if (suffix_start == semantic.size()) {
        return {std::move(semantic), 0};
    }

    const std::string suffix = semantic.substr(suffix_start);
    semantic.resize(suffix_start);
    if (semantic.empty()) {
        return {"TEXCOORD", static_cast<UINT>(std::stoul(suffix))};
    }
    return {std::move(semantic), static_cast<UINT>(std::stoul(suffix))};
}

static D3D11InputSemantic d3d11_semantic_for_logical_attribute(std::string semantic) {
    D3D11InputSemantic parsed = parse_d3d11_semantic(std::move(semantic));
    if (parsed.name == "UV" || parsed.name == "TEXCOORD") {
        parsed.name = "TEXCOORD";
        return parsed;
    }
    if (parsed.name == "COLOR" || parsed.name == "COLOUR") {
        parsed.name = "COLOR";
        return parsed;
    }
    if (parsed.name == "POSITION" || parsed.name == "POS") {
        parsed.name = "POSITION";
        return parsed;
    }
    if (parsed.name == "NORMAL") {
        parsed.name = "NORMAL";
        return parsed;
    }
    if (parsed.name == "TANGENT") {
        parsed.name = "TANGENT";
        return parsed;
    }
    if (parsed.name == "JOINT" || parsed.name == "JOINTS") {
        parsed.name = "BLENDINDICES";
        return parsed;
    }
    if (parsed.name == "WEIGHT" || parsed.name == "WEIGHTS") {
        parsed.name = "BLENDWEIGHT";
        return parsed;
    }
    return parsed;
}

D3D11InputSemantic semantic_for_attribute(const VertexAttribute& attr) {
    if (!attr.semantic.empty()) {
        return d3d11_semantic_for_logical_attribute(attr.semantic);
    }
    std::string_view standard_semantic = standard_vertex_semantic_for_location(attr.location);
    if (!standard_semantic.empty()) {
        return d3d11_semantic_for_logical_attribute(std::string(standard_semantic));
    }
    return {"TEXCOORD", attr.location};
}

D3D11InputSemantic semantic_for_attribute(const VertexAttributeDesc& attr) {
    if (attr.semantic && attr.semantic[0] != '\0') {
        return d3d11_semantic_for_logical_attribute(attr.semantic);
    }
    std::string_view standard_semantic = standard_vertex_semantic_for_location(attr.location);
    if (!standard_semantic.empty()) {
        return d3d11_semantic_for_logical_attribute(std::string(standard_semantic));
    }
    return {"TEXCOORD", attr.location};
}

std::vector<D3D11InputSemantic> reflect_d3d11_vertex_inputs(const D3D11ShaderModule& vs) {
    std::vector<D3D11InputSemantic> out;
    if (vs.bytecode.empty()) {
        return out;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
    HRESULT hr = D3DReflect(
        vs.bytecode.data(),
        vs.bytecode.size(),
        __uuidof(ID3D11ShaderReflection),
        reinterpret_cast<void**>(reflection.GetAddressOf()));
    if (FAILED(hr) || !reflection) {
        tc::Log::error(
            "D3D11RenderDevice::create_pipeline: D3DReflect failed for vertex shader: HRESULT=0x%08X",
            static_cast<unsigned>(hr));
        return out;
    }

    D3D11_SHADER_DESC shader_desc{};
    hr = reflection->GetDesc(&shader_desc);
    if (FAILED(hr)) {
        tc::Log::error(
            "D3D11RenderDevice::create_pipeline: shader reflection GetDesc failed: HRESULT=0x%08X",
            static_cast<unsigned>(hr));
        return out;
    }

    out.reserve(shader_desc.InputParameters);
    for (UINT i = 0; i < shader_desc.InputParameters; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC param{};
        hr = reflection->GetInputParameterDesc(i, &param);
        if (FAILED(hr) || !param.SemanticName) {
            continue;
        }
        if (param.SystemValueType != D3D_NAME_UNDEFINED) {
            continue;
        }
        out.push_back({param.SemanticName, param.SemanticIndex});
    }
    return out;
}

std::vector<D3D11SignatureParam> reflect_d3d11_signature(
    const D3D11ShaderModule& shader,
    bool output_signature)
{
    std::vector<D3D11SignatureParam> out;
    if (shader.bytecode.empty()) {
        return out;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
    HRESULT hr = D3DReflect(
        shader.bytecode.data(),
        shader.bytecode.size(),
        __uuidof(ID3D11ShaderReflection),
        reinterpret_cast<void**>(reflection.GetAddressOf()));
    if (FAILED(hr) || !reflection) {
        tc::Log::error(
            "D3D11RenderDevice::create_pipeline: D3DReflect failed for shader '%s': HRESULT=0x%08X",
            shader.debug_name.c_str(),
            static_cast<unsigned>(hr));
        return out;
    }

    D3D11_SHADER_DESC shader_desc{};
    hr = reflection->GetDesc(&shader_desc);
    if (FAILED(hr)) {
        tc::Log::error(
            "D3D11RenderDevice::create_pipeline: shader reflection GetDesc failed for '%s': HRESULT=0x%08X",
            shader.debug_name.c_str(),
            static_cast<unsigned>(hr));
        return out;
    }

    const UINT count = output_signature
        ? shader_desc.OutputParameters
        : shader_desc.InputParameters;
    out.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC param{};
        hr = output_signature
            ? reflection->GetOutputParameterDesc(i, &param)
            : reflection->GetInputParameterDesc(i, &param);
        if (FAILED(hr) || !param.SemanticName) {
            continue;
        }
        out.push_back({
            param.SemanticName,
            param.SemanticIndex,
            param.Register,
            param.Mask,
            param.SystemValueType,
        });
    }
    return out;
}

static const D3D11SignatureParam* find_signature_param(
    const std::vector<D3D11SignatureParam>& params,
    const D3D11SignatureParam& needle)
{
    for (const D3D11SignatureParam& param : params) {
        if (uppercase_ascii(param.semantic) == uppercase_ascii(needle.semantic) &&
            param.semantic_index == needle.semantic_index) {
            return &param;
        }
    }
    return nullptr;
}

bool signatures_have_link_mismatch(
    const std::vector<D3D11SignatureParam>& vs_outputs,
    const std::vector<D3D11SignatureParam>& ps_inputs)
{
    bool mismatch = false;
    for (const D3D11SignatureParam& ps_input : ps_inputs) {
        if (ps_input.system_value != D3D_NAME_UNDEFINED) {
            continue;
        }
        const D3D11SignatureParam* vs_output = find_signature_param(vs_outputs, ps_input);
        if (!vs_output) {
            mismatch = true;
            continue;
        }
        if ((vs_output->mask & ps_input.mask) != ps_input.mask) {
            mismatch = true;
        }
    }
    return mismatch;
}

void log_d3d11_shader_signatures(
    const char* reason,
    const D3D11ShaderModule& vs,
    const D3D11ShaderModule& ps,
    const std::vector<D3D11SignatureParam>& vs_outputs,
    const std::vector<D3D11SignatureParam>& ps_inputs)
{
    tc::Log::warn(
        "D3D11RenderDevice::create_pipeline: shader signature diagnostic (%s): vs='%s' ps='%s'",
        reason,
        vs.debug_name.c_str(),
        ps.debug_name.c_str());
    for (size_t i = 0; i < vs_outputs.size(); ++i) {
        const D3D11SignatureParam& p = vs_outputs[i];
        tc::Log::warn(
            "  vs_out[%zu]: %s%u reg=%u mask=0x%02X sys=%u",
            i,
            p.semantic.c_str(),
            p.semantic_index,
            p.register_index,
            static_cast<unsigned>(p.mask),
            static_cast<unsigned>(p.system_value));
    }
    for (size_t i = 0; i < ps_inputs.size(); ++i) {
        const D3D11SignatureParam& p = ps_inputs[i];
        tc::Log::warn(
            "  ps_in[%zu]: %s%u reg=%u mask=0x%02X sys=%u",
            i,
            p.semantic.c_str(),
            p.semantic_index,
            p.register_index,
            static_cast<unsigned>(p.mask),
            static_cast<unsigned>(p.system_value));
    }
}

void log_d3d11_input_layout_failure(
    HRESULT hr,
    const std::vector<D3D11_INPUT_ELEMENT_DESC>& input_elements,
    const std::vector<D3D11InputSemantic>& reflected_inputs
) {
    tc::Log::error(
        "D3D11RenderDevice::create_pipeline: CreateInputLayout failed: HRESULT=0x%08X "
        "elements=%zu reflected_inputs=%zu",
        static_cast<unsigned>(hr),
        input_elements.size(),
        reflected_inputs.size());
    for (size_t i = 0; i < input_elements.size(); ++i) {
        const D3D11_INPUT_ELEMENT_DESC& element = input_elements[i];
        tc::Log::error(
            "  layout[%zu]: semantic=%s%u format=%u slot=%u offset=%u class=%u step=%u",
            i,
            element.SemanticName ? element.SemanticName : "<null>",
            element.SemanticIndex,
            static_cast<unsigned>(element.Format),
            element.InputSlot,
            element.AlignedByteOffset,
            static_cast<unsigned>(element.InputSlotClass),
            element.InstanceDataStepRate);
    }
    for (size_t i = 0; i < reflected_inputs.size(); ++i) {
        tc::Log::error(
            "  vs_input[%zu]: semantic=%s%u",
            i,
            reflected_inputs[i].name.c_str(),
            reflected_inputs[i].index);
    }
}

} // namespace tgfx::d3d11_internal
