#pragma once

#include <string>

#include "transform.hpp"
#include "vector/vector.hpp"

struct Param {
    double amt;
    int smp_lim;
    int ext;
    int geo_cache;
    int cache_purge;
    bool print_info;
};

struct Context {
    std::string name;
    Vec2<double> res;
    Vec2<double> pivot;
    int id, idx, num;
    int frame;
    int range;

    constexpr Context(const std::string &name_, double w, double h, double cx, double cy, int id_, int idx_, int num_,
                      int frame_, int range_) noexcept :
        name(name_), res(w, h), pivot(cx, cy), id(id_), idx(idx_), num(num_), frame(frame_), range(range_) {}
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

    constexpr Flow(const Transform &xform_curr, const Transform &xform_prev, const Geo &curr_, Geo *data_) noexcept :
        data(data_), curr(curr_), xform{xform_curr, xform_prev}, geo{&curr, &curr} {}

    constexpr void write_data(const Geo &v) noexcept {
        if (data)
            *data = v;
    }

    constexpr const Geo *read_data() noexcept { return data && data->is_valid() ? data : nullptr; }

    [[nodiscard]] Delta delta() noexcept {
        xform.curr.set_geo(*geo.curr);
        xform.prev.set_geo(*geo.prev);
        return Delta(xform.curr, xform.prev);
    }
};
