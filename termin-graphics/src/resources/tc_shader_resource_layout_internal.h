#pragma once

#include "tgfx/resources/tc_shader_registry.h"

int tc_shader_resource_requirement_compare(const void* a, const void* b);

void tc_shader_free_resource_requirement_array(
    tc_shader_resource_requirement* requirements,
    uint32_t count);
