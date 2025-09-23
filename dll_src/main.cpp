#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string>

#include <lua.hpp>

#include "geo_utils.hpp"
#include "lua_func.hpp"
#include "structs.hpp"

static auto geo_map = GeoMap<8>();

static void
set_init_geo(std::uint32_t ext, const Input input) noexcept {
    int flag = 1;
    std::array<Geo, 3> geos{};

    for (std::size_t i = 0; i <= ext; ++i) {
        if (auto g = geo_map.read(input.obj_id, input.obj_idx, i + 1))
            geos[i] = *g;
        else
            flag = 0;
    }

    if (!flag)
        return;

    switch (ext) {
        case 1u:
            geo_map.write(input.obj_id, input.obj_idx, 0, geos[0] * 2.0 - geos[1]);
            return;
        case 2u:
            geo_map.write(input.obj_id, input.obj_idx, 0, geos[0] * 3.0 - geos[1] * 3.0 + geos[2]);
            return;
        default:
            return;
    }
}

static Delta
calc_delta(const Param &param, Input &input, std::size_t pos) noexcept {
    input.tf_curr.set_geo(input.geo_curr);

    if (auto geo = param.geo_cache ? geo_map.read(input.obj_id, input.obj_idx, pos) : nullptr)
        input.tf_prev.set_geo(*geo);
    else
        input.tf_prev.set_geo(input.geo_curr);

    return Delta(input.tf_curr, input.tf_prev);
}

static std::array<Vec2<float>, 2>
calc_size(Delta &delta, float amt, const Input &input) noexcept {
    std::array<Vec2<float>, 2> margin{};

    const float h_amt = amt * 0.5f;
    std::array<Mat3<float>, 2> htm_data{delta.calc_htm(h_amt), delta.calc_htm(amt)};
    std::array<Vec2<float>, 2> drift_data{delta.calc_drift(h_amt), delta.calc_drift(amt)};

    for (std::size_t i = 0; i < 2; ++i) {
        auto c_prev = Vec3<float>(-input.pivot + drift_data[i], 1.0f);
        auto pos = (htm_data[i] * c_prev).to_vec2() + input.pivot;
        auto bbox = (htm_data[i].to_mat2().abs()) * input.res;

        auto diff = (bbox - input.res) * 0.5f;
        margin[0] = margin[0].max((diff - pos).ceil());
        margin[1] = margin[1].max((diff + pos).ceil());
    }

    return margin;
}

static void
cleanup_geo(const Param &param, const Input &input) {
    if (!input.is_last[0])
        return;

    switch (param.geo_ctrl) {
        case 1:
            if (input.is_last[1])
                geo_map.clear(input.obj_id);
            return;
        case 2:
            geo_map.clear();
            return;
        case 3:
            geo_map.clear(input.obj_id);
            return;
        default:
            return;
    }
}

int
process_motion_blur(lua_State *L) {
    Obj obj(L);
    const auto param = obj.get_param();
    auto input = obj.get_input(param.ext);

    if (input.obj_idx >= input.obj_num)
        return 0;

    bool flag = param.is_valid && (param.ext || input.frame);
    std::uint32_t req_smp = 0u;

    geo_map.resize(input.obj_id, input.obj_idx, input.obj_num, param.geo_cache);

    if (param.geo_cache == 1u || (param.geo_cache == 2u && param.ext && input.frame <= param.ext))
        geo_map.write(input.obj_id, input.obj_idx, input.frame + 1, input.geo_curr);

    if (flag) {
        const float amt = param.shutter_angle / 360.0f;

        std::array<Vec2<float>, 2> margin{};
        Vec2<float> delta_res{};
        std::uint32_t smp = 0u;

        if (param.geo_cache && !input.frame)
            set_init_geo(param.ext, input);

        auto delta = calc_delta(param, input, (param.geo_cache == 2u && input.frame) ? 7 : input.frame);

        if (delta.is_moved()) {
            margin = calc_size(delta, amt, input);
            delta_res = margin[0] + margin[1];
            req_smp = static_cast<std::uint32_t>(delta_res.norm(2));
            smp = std::min(req_smp, param.smp_lim - 1u);
        }

        if (smp) {
            auto init_htm = delta.calc_htm(amt, smp, true);
            auto drift = delta.calc_drift(amt, smp);

            Vec2<float> res_new = input.res;
            Vec2<float> pivot_new = input.pivot;
            if (param.resize) {
                auto c_new = input.geo_curr.get_center() + (margin[0] - margin[1]) * 0.5f;
                obj.resize(margin, c_new);
                pivot_new = input.tf_curr.get_center() + c_new;
                res_new += delta_res;
            }

            Vec2<float> ps_pivot = res_new * 0.5f + pivot_new;
            std::vector<float> constants = {
                    init_htm(0, 0),   init_htm(0, 1),   init_htm(0, 2),          0.0f,
                    init_htm(1, 0),   init_htm(1, 1),   init_htm(1, 2),          0.0f,
                    init_htm(2, 0),   init_htm(2, 1),   init_htm(2, 2),          0.0f,
                    drift.get_x(),    drift.get_y(),    res_new.get_x(),         res_new.get_y(),
                    ps_pivot.get_x(), ps_pivot.get_y(), static_cast<float>(smp), param.mix};

            obj.pixel_shader(param.shader_name, constants);
        }
    }

    if (param.geo_cache == 2u) {
        if (auto geo = geo_map.read(input.obj_id, input.obj_idx, 7); !geo || !geo->is_cached(input.frame))
            geo_map.write(input.obj_id, input.obj_idx, 7, input.geo_curr);
    }

    cleanup_geo(param, input);

    if (param.print_info) {
        std::string info = std::format(
                "[INFO]\n"
                "Object ID       : {}\n"
                "Index           : {}\n"
                "Required Samples: {}",
                input.obj_id, input.obj_idx, req_smp);
        obj.print(info);
    }
    return 0;
}

static luaL_Reg functions[] = {{"process_motion_blur", process_motion_blur}, {nullptr, nullptr}};

extern "C" int
luaopen_ObjectMotionBlur_LK(lua_State *L) {
    luaL_register(L, "ObjectMotionBlur_LK", functions);
    return 1;
}
