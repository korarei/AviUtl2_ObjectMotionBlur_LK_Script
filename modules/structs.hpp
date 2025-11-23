#pragma once

#include <algorithm>
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

    constexpr Param(double amt_, int smp_lim_, int ext_, int geo_cache_, int cache_purge_, bool print_info_) noexcept :
        amt(std::max(amt_, 0.0)),
        smp_lim(std::max(smp_lim_, 1)),
        ext(std::clamp(ext_, 0, 2)),
        geo_cache(std::clamp(geo_cache_, 0, 2)),
        cache_purge(std::clamp(cache_purge_, 0, 3)),
        print_info(print_info_) {}
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

    [[nodiscard]] constexpr const Geo *read_data() const noexcept { return data && data->is_valid() ? data : nullptr; }

    [[nodiscard]] Delta delta() noexcept {
        xform.curr.set_geo(*geo.curr);
        xform.prev.set_geo(*geo.prev);
        return Delta(xform.curr, xform.prev);
    }
};
