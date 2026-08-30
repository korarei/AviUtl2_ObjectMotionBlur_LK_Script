#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)  // 非標準の無名構造体 (filter2.h FILTER_ITEM_COLOR)
#include <filter2.h>
#pragma warning(pop)

#include "render.hpp"

namespace blur::scene {
static_assert(sizeof(PIXEL_RGBA) == sizeof(uint32_t));

struct Instance {
    struct Frame {
        std::optional<int> frame = std::nullopt;
        std::vector<PIXEL_RGBA> image;
    };

    renderer::ID id{};
    int section = -1;
    Frame prev{};
    Frame curr{};
};
}  // namespace blur::scene
