#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "transform_utils.hpp"
#include "vector_2d.hpp"

struct Param {
    std::string shader_name;
    float shutter_angle;
    std::uint32_t smp_lim;
    std::uint32_t ext;
    bool resize;
    std::uint32_t geo_cache;
    std::uint32_t cache_ctrl;
    float mix;
    bool print_info;
    bool is_valid;
};

struct Input {
    Vec2<float> res;
    Vec2<float> pivot;

    std::size_t obj_id, obj_idx, obj_num;
    std::size_t frame;
    std::array<bool, 2> is_last;

    Transform tf_curr, tf_prev;
    Geo geo_curr;
};
