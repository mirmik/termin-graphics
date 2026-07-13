#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <vector>

#include <termin/image/image_decode.hpp>

#include "guard_main.h"

namespace {

std::atomic_size_t tracked_allocations = 0;
std::atomic_size_t failing_allocation = 0;
std::atomic_bool track_allocations = false;

bool decode_throws(std::span<const std::uint8_t> bytes) {
    try {
        (void)termin::image::decode_rgba8(bytes, "malformed-image");
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

} // namespace

void* operator new(std::size_t size) {
    if (track_allocations.load(std::memory_order_relaxed)) {
        const std::size_t allocation = tracked_allocations.fetch_add(1, std::memory_order_relaxed) + 1;
        if (allocation == failing_allocation.load(std::memory_order_relaxed)) {
            throw std::bad_alloc();
        }
    }

    if (void* memory = std::malloc(size)) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

TEST_CASE("image codecs reject truncated JPEG without escaping through C++ frames") {
    constexpr std::array<std::uint8_t, 4> truncated_jpeg = {0xFF, 0xD8, 0xFF, 0xDB};

    CHECK(decode_throws(truncated_jpeg));
}

TEST_CASE("image codecs reject truncated PNG") {
    constexpr std::array<std::uint8_t, 12> truncated_png = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x00,
    };

    CHECK(decode_throws(truncated_png));
}

TEST_CASE("PNG encoder output is decoded as RGBA8") {
    const std::vector<std::uint8_t> pixels = {
        0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80,
    };

    const std::vector<std::uint8_t> encoded = termin::image::encode_png_rgba8(pixels, 2, 1);
    const termin::image::DecodedImage decoded = termin::image::decode_rgba8(encoded, "round-trip");

    CHECK(decoded.width == 2);
    CHECK(decoded.height == 1);
    CHECK(decoded.channels == 4);
    CHECK(decoded.format == "png");
    CHECK(decoded.pixels == pixels);
}

TEST_CASE("PNG write callback reports allocation failure without crossing C frames") {
    const std::vector<std::uint8_t> pixels = {
        0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80,
    };

    tracked_allocations.store(0, std::memory_order_relaxed);
    failing_allocation.store(0, std::memory_order_relaxed);
    track_allocations.store(true, std::memory_order_relaxed);
    const std::vector<std::uint8_t> encoded = termin::image::encode_png_rgba8(pixels, 2, 1);
    track_allocations.store(false, std::memory_order_relaxed);
    CHECK_FALSE(encoded.empty());

    const std::size_t allocation_count = tracked_allocations.load(std::memory_order_relaxed);
    REQUIRE(allocation_count > 0);

    tracked_allocations.store(0, std::memory_order_relaxed);
    failing_allocation.store(allocation_count, std::memory_order_relaxed);
    track_allocations.store(true, std::memory_order_relaxed);
    bool caught_bad_alloc = false;
    try {
        (void)termin::image::encode_png_rgba8(pixels, 2, 1);
    } catch (const std::bad_alloc&) {
        caught_bad_alloc = true;
    }
    track_allocations.store(false, std::memory_order_relaxed);

    CHECK(caught_bad_alloc);
}

GUARD_TEST_MAIN();
