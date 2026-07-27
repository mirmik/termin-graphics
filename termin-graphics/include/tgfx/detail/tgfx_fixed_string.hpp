#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace termin::detail {

template<std::size_t Capacity>
void copy_c_string_truncated(
    char (&destination)[Capacity],
    const char* source) noexcept
{
    static_assert(Capacity > 0);

    const char* resolved_source = source ? source : "";
    std::size_t length = 0;
    while (length < Capacity - 1 && resolved_source[length] != '\0') {
        ++length;
    }

    std::memmove(destination, resolved_source, length);
    std::fill(destination + length, destination + Capacity, '\0');
}

} // namespace termin::detail
