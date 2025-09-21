#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "transform_utils.hpp"
#include "utils.hpp"
#include "vector_2d.hpp"
#include "vector_3d.hpp"

struct CParam {
    float shutter_angle;
    int smp_lim;
    int ext;
    bool resize;
    int geo_cache;
    int geo_ctrl;
};

struct CInput {
    float w, h;
    float px, py;
    int obj_id, obj_idx, obj_num;
    int frame, total_frame;
    float curr[6];
    float prev[6];
    float geo[6];
};

struct COutput {
    int margin[4];
    float init_htm[3][3];
    float drift[2];
    int smp;
    int req_smp;

    constexpr COutput() noexcept = default;
    constexpr COutput(const std::array<int, 4> &margin_, const Mat3<float> &init_htm_, const Vec2<float> &drift_,
                      std::uint32_t smp_, std::uint32_t req_smp_) noexcept :
        smp(static_cast<int>(smp_)), req_smp(static_cast<int>(req_smp_)) {
        std::copy(margin_.begin(), margin_.end(), margin);
        for (std::size_t i = 0; i < 3; ++i) std::copy(init_htm_[i].begin(), init_htm_[i].end(), init_htm[i]);
        std::copy(drift_.begin(), drift_.end(), drift);
    }
};

struct Param {
    float shutter_angle;
    std::uint32_t smp_lim;
    std::uint32_t ext;
    bool resize;
    std::uint32_t geo_cache;
    std::uint32_t geo_ctrl;
    bool is_valid;

    constexpr Param(const CParam &c_param) noexcept :
        shutter_angle(std::clamp(c_param.shutter_angle, 0.0f, 360.0f)),
        smp_lim(static_cast<std::uint32_t>(std::max(c_param.smp_lim, 1))),
        ext(static_cast<std::uint32_t>(std::clamp(c_param.ext, 0, 2))),
        resize(c_param.resize),
        geo_cache(static_cast<std::uint32_t>(std::clamp(c_param.geo_cache, 0, 2))),
        geo_ctrl(static_cast<std::uint32_t>(std::clamp(c_param.geo_ctrl, 0, 3))),
        is_valid(shutter_angle > 1.0e-4 && smp_lim > 1u) {}
};

struct Input {
    Vec2<float> res;
    Vec2<float> pivot;

    std::size_t obj_id, obj_idx, obj_num;
    std::size_t frame;
    std::array<bool, 2> is_last;

    Transform tf_curr, tf_prev;
    Geo geo_curr;

    constexpr Input(const CInput &c_input) noexcept :
        res(c_input.w, c_input.h),
        pivot(c_input.px, c_input.py),
        obj_id(static_cast<std::size_t>(std::max(c_input.obj_id, 0))),
        obj_idx(static_cast<std::size_t>(c_input.obj_idx)),
        obj_num(static_cast<std::size_t>(c_input.obj_num)),
        frame(static_cast<std::size_t>(c_input.frame)),
        is_last(c_input.obj_idx == c_input.obj_num - 1, c_input.frame == c_input.total_frame - 1),
        tf_curr(std::to_array(c_input.curr)),
        tf_prev(std::to_array(c_input.prev)),
        geo_curr(true, frame, std::to_array(c_input.geo)) {}
};
