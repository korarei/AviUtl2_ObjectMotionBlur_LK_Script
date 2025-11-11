#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "transform_utils.hpp"
#include "vector_2d.hpp"

struct Param {
    double amt;
    int smp_lim;
    int ext;
    int geo_cache;
    int cache_ctrl;
    bool print_info;
};

struct Context {
    Vec2<double> res;
    Vec2<double> pivot;
    int id, idx, num;
    int frame;
    int range;

    constexpr Context(double w, double h, double cx, double cy, int id_, int idx_, int num_, int frame_,
                      int range_) noexcept :
        res(w, h), pivot(cx, cy), id(id_), idx(idx_), num(num_), frame(frame_), range(range_) {}
};

template <typename T>
struct Data {
    T curr;
    T prev;
};

struct Flow {
private:
    Geo *data;
    Geo curr;

public:
    Data<Transform> xform;
    Data<const Geo *> geo;

    constexpr Flow(Geo *data_, const Geo &curr_, const Transform &xform_curr, const Transform &xform_prev) noexcept :
        data(data_), curr(curr_), xform{xform_curr, xform_prev}, geo{&curr, data} {}

    constexpr void write_data(const Geo &v) noexcept { *data = v; }

    [[nodiscard]] Delta delta() noexcept {
        xform.curr.set_geo(*geo.curr);
        xform.prev.set_geo(*geo.prev);
        return Delta(xform.curr, xform.prev);
    }
};
