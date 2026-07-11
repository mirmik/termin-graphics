#pragma once

#include <nanobind/nanobind.h>

#include "termin/bindings/tc_value_helpers.hpp"
#include "termin/render/unknown_pass.hpp"

namespace termin {

namespace nb = nanobind;

inline nb::dict serialize_unknown_pass_envelope(tc_pass* raw) {
    auto* unknown = dynamic_cast<UnknownPass*>(CxxFramePass::from_tc(raw));
    if (!unknown) throw std::runtime_error("incompatible UnknownPass object");

    nb::dict result;
    result["type"] = unknown->original_type;
    result["pass_name"] = raw->pass_name ? raw->pass_name : "";
    result["enabled"] = raw->enabled;
    result["passthrough"] = raw->passthrough;
    result["viewport_name"] = raw->viewport_name ? raw->viewport_name : "";
    result["data"] = tc_value_to_py(&unknown->original_data);

    nb::dict graph;
    nb::list reads;
    for (const std::string& value : unknown->original_reads) reads.append(value);
    graph["reads"] = reads;
    nb::list writes;
    for (const std::string& value : unknown->original_writes) writes.append(value);
    graph["writes"] = writes;
    nb::list aliases;
    for (const auto& [read, write] : unknown->original_inplace_aliases) {
        nb::list pair;
        pair.append(read);
        pair.append(write);
        aliases.append(pair);
    }
    graph["inplace_aliases"] = aliases;
    nb::list symbols;
    for (const std::string& value : unknown->original_internal_symbols) symbols.append(value);
    graph["internal_symbols"] = symbols;

    nb::list specs;
    for (const ResourceSpec& spec : unknown->original_resource_specs) {
        nb::dict item;
        item["resource"] = spec.resource;
        item["resource_type"] = spec.resource_type;
        if (spec.size) item["size"] = nb::make_tuple(spec.size->first, spec.size->second);
        if (spec.clear_color) {
            const auto& color = *spec.clear_color;
            item["clear_color"] = nb::make_tuple(color[0], color[1], color[2], color[3]);
        }
        if (spec.clear_depth) item["clear_depth"] = *spec.clear_depth;
        if (spec.format) item["format"] = *spec.format;
        item["samples"] = spec.samples;
        item["viewport_name"] = spec.viewport_name;
        item["scale"] = spec.scale;
        item["filter"] = static_cast<int>(spec.filter);
        specs.append(item);
    }
    graph["resource_specs"] = specs;
    result["_unknown_graph"] = graph;
    return result;
}

} // namespace termin
