#include "guard_main.h"

#include <tgfx/detail/tgfx_fixed_string.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>

TEST_CASE("fixed C string copy preserves short strings and clears the tail")
{
    char destination[8];
    std::fill(std::begin(destination), std::end(destination), '\x7f');

    termin::detail::copy_c_string_truncated(destination, "abc");

    CHECK(std::strcmp(destination, "abc") == 0);
    for (std::size_t index = 3; index < std::size(destination); ++index) {
        CHECK_EQ(destination[index], '\0');
    }
}

TEST_CASE("fixed C string copy accepts the exact payload capacity")
{
    char destination[8] = {};

    termin::detail::copy_c_string_truncated(destination, "1234567");

    CHECK(std::strcmp(destination, "1234567") == 0);
    CHECK_EQ(destination[7], '\0');
}

TEST_CASE("fixed C string copy truncates oversize input and terminates it")
{
    char destination[8] = {};

    termin::detail::copy_c_string_truncated(destination, "123456789");

    CHECK(std::strcmp(destination, "1234567") == 0);
    CHECK_EQ(destination[7], '\0');
}

TEST_CASE("fixed C string copy treats null input as an empty string")
{
    char destination[8];
    std::fill(std::begin(destination), std::end(destination), '\x7f');

    termin::detail::copy_c_string_truncated(destination, nullptr);

    for (char value : destination) {
        CHECK_EQ(value, '\0');
    }
}
