#include <algorithm>
#include <array>
#include <cstdint>

#include "geo_utils.hpp"
#include "structs.hpp"

static auto geo_map = GeoMap<8>();

inline static constexpr std::uint32_t
make_key(std::uint32_t obj_id, std::uint32_t obj_idx) noexcept {
    std::uint32_t id_t = (obj_idx & 0x3FFFFu) << 14;
    std::uint32_t id_b = obj_id & 0x3FFFu;
    std::uint32_t id = id_t | id_b;
    return id;
}

static void
set_init_geo(std::uint32_t ext, std::uint32_t k) noexcept {
    const std::array<std::uint32_t, 2> key{k, 0u};

    int flag = 1;
    std::array<Geo, 3> geos{};

    for (std::uint32_t i = 0u; i <= ext; ++i) {
        if (auto g = geo_map.read({k, i + 1u}))
            geos[i] = *g;
        else
            flag = 0;
    }

    if (!flag)
        return;

    switch (ext) {
        case 1u:
            geo_map.write(key, geos[0] * 2.0 - geos[1]);
            return;
        case 2u:
            geo_map.write(key, geos[0] * 3.0 - geos[1] * 3.0 + geos[2]);
            return;
        default:
            return;
    }
}

static Delta
calc_delta(const Param &param, Input &input, const std::array<std::uint32_t, 2> &key) noexcept {
    input.tf_curr.set_geo(input.geo_curr);

    if (auto geo = param.geo_cache ? geo_map.read(key) : nullptr)
        input.tf_prev.set_geo(*geo);
    else
        input.tf_prev.set_geo(input.geo_curr);

    return Delta(input.tf_curr, input.tf_prev);
}

static std::array<int, 4>
calc_size(Delta &delta, float amt, const Input &input) noexcept {
    std::array<int, 4> margin{};

    auto htm = delta.calc_htm(amt);
    auto c_prev = Vec3<float>(delta.get_center(), 1.0f);
    auto pos = (htm * c_prev).to_vec2() + input.pivot;
    auto bbox = (htm.to_mat2().abs()) * input.res;

    auto diff = (bbox - input.res) * 0.5f;
    auto upper_left = static_cast<Vec2<int>>((diff - pos).ceil());
    auto lower_right = static_cast<Vec2<int>>((diff + pos).ceil());

    margin[0] = std::max(upper_left.get_y(), 0);
    margin[1] = std::max(lower_right.get_y(), 0);
    margin[2] = std::max(upper_left.get_x(), 0);
    margin[3] = std::max(lower_right.get_x(), 0);

    return margin;
}

static void
cleanup_geo(const Param &param, const Input &input) {
    constexpr std::uint32_t mask = 0x3FFFu;

    if (!input.is_last[0])
        return;

    if (param.geo_cache) {
        switch (param.geo_ctrl) {
            case 1:
                if (input.is_last[1])
                    geo_map.clear(input.obj_id, mask);
                break;
            case 2:
                geo_map.clear();
                break;
            case 3:
                geo_map.clear(input.obj_id, mask);
                break;
            default:
                break;
        }
    } else if (geo_map.has_key0(input.obj_id)) {
        geo_map.clear(input.obj_id, mask);
    }
}

extern "C" int
process_motion_blur(const CParam *c_param, const CInput *c_input, COutput *c_output) {
    if (!c_param || !c_input || !c_output)
        return -1;

    const auto param = Param(*c_param);
    auto input = Input(*c_input);

    std::uint32_t k = make_key(input.obj_id, input.obj_idx);
    bool flag = param.is_valid && (param.ext || input.frame);

    if (param.geo_cache == 1u || (param.geo_cache == 2u && param.ext && input.frame <= param.ext))
        geo_map.write({k, input.frame + 1u}, input.geo_curr);

    if (flag) {
        const float amt = param.shutter_angle / 360.0f;

        std::array<int, 4> margin{};
        std::uint32_t req_smp = 0u;
        std::uint32_t smp = 0u;

        if (param.geo_cache && !input.frame)
            set_init_geo(param.ext, k);

        auto delta = calc_delta(param, input, {k, (param.geo_cache == 2u && input.frame) ? 7u : input.frame});

        if (delta.is_moved()) {
            margin = calc_size(delta, amt, input);
            req_smp = static_cast<std::uint32_t>(Vec2<int>(margin[2] + margin[3], margin[0] + margin[1]).norm(2));
            smp = std::min(req_smp, param.smp_lim - 1u);
        }

        if (smp) {
            auto init_htm = delta.calc_htm(amt, smp, true);
            auto drift = delta.calc_drift(amt, smp);

            *c_output = COutput(margin, init_htm, drift, smp, req_smp);
        } else {
            flag = false;
            *c_output = COutput();
        }
    } else {
        *c_output = COutput();
    }

    if (param.geo_cache == 2u)
        if (auto geo = geo_map.read({k, 7u}); !geo || !geo->is_cached(input.frame))
            geo_map.write({k, 7u}, input.geo_curr);

    cleanup_geo(param, input);

    return !flag;
}
