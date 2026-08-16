#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)  // 非標準の無名構造体 (filter2.h FILTER_ITEM_COLOR)
#include <filter2.h>
#pragma warning(pop)

namespace blur::scene {
struct Image {
    int w = 0, h = 0;
    std::vector<PIXEL_RGBA> data;
};

static_assert(sizeof(PIXEL_RGBA) == sizeof(uint32_t));

struct Instance {
    int section = -1;
    int frame = 0;
    Image image{};
};
}  // namespace blur::scene
