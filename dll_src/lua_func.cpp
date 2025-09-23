#include "lua_func.hpp"

#include <algorithm>

Obj::Obj(lua_State *L) : L(L) {
    lua_getglobal(L, "obj");
    obj_ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

Obj::~Obj() { luaL_unref(L, LUA_REGISTRYINDEX, obj_ref); }

bool
Obj::get_saving() {
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj_ref);
    lua_getfield(L, -1, "getinfo");
    lua_pushstring(L, "saving");
    lua_call(L, 1, 1);
    bool saving = lua_toboolean(L, -1);
    lua_pop(L, 2);
    return saving;
}

float
Obj::get_val(const char *target, std::optional<double> time) {
    lua_getfield(L, -1, "getvalue");
    lua_pushstring(L, target);

    if (time) {
        lua_pushnumber(L, *time);
        lua_call(L, 2, 1);
    } else {
        lua_call(L, 1, 1);
    }

    float v = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return v;
}

Param
Obj::get_param() {
    name = get_string(1, "motion_blur");
    float shutter_angle = static_cast<float>(std::clamp(get_number(2, 180), 0.0, 360.0));
    std::uint32_t render_smp_lim = static_cast<std::uint32_t>(std::max(get_integer(3, 256ll), 1ll));
    std::uint32_t preview_smp_lim = static_cast<std::uint32_t>(std::max(get_integer(4, 0ll), 0ll));
    std::uint32_t ext = static_cast<std::uint32_t>(std::clamp(get_integer(5, 2ll), 0ll, 2ll));
    bool resize = get_boolean(6, true);
    std::uint32_t geo_cache = static_cast<std::uint32_t>(std::clamp(get_integer(7, 0ll), 0ll, 2ll));
    std::uint32_t geo_ctrl = static_cast<std::uint32_t>(std::clamp(get_integer(8, 0ll), 0ll, 3ll));
    float mix = static_cast<float>(std::clamp(get_number(9, 0.0), 0.0, 1.0));
    bool print_info = get_boolean(10, false);

    std::uint32_t smp_lim = (!preview_smp_lim || get_saving()) ? render_smp_lim : preview_smp_lim;
    bool is_valid = shutter_angle > 1.0e-4 && smp_lim > 1u;

    return Param(shutter_angle, smp_lim, ext, resize, geo_cache, geo_ctrl, mix, print_info, is_valid);
}

Input
Obj::get_input(std::uint32_t ext) {
    Input input{};

    lua_rawgeti(L, LUA_REGISTRYINDEX, obj_ref);

    lua_getfield(L, -1, "getpixel");
    lua_call(L, 0, 2);
    input.res = Vec2<float>(static_cast<float>(lua_tonumber(L, -2)), static_cast<float>(lua_tonumber(L, -1)));
    lua_pop(L, 2);

    input.obj_id = static_cast<std::size_t>(std::max(get_integer(11, 0ll), 0ll));
    lua_getfield(L, -1, "index");
    lua_getfield(L, -2, "num");
    input.obj_idx = static_cast<std::size_t>(lua_tointeger(L, -2));
    input.obj_num = static_cast<std::size_t>(lua_tointeger(L, -1));
    lua_pop(L, 2);

    lua_getfield(L, -1, "frame");
    lua_getfield(L, -2, "totalframe");
    lua_getfield(L, -3, "framerate");
    input.frame = static_cast<std::size_t>(lua_tointeger(L, -3));
    std::size_t total_frame = static_cast<std::size_t>(lua_tointeger(L, -2));
    double framerate = lua_tonumber(L, -1);
    lua_pop(L, 3);

    input.is_last = {input.obj_idx == input.obj_num - 1, input.frame == total_frame - 1};

    double dt = 1.0 / framerate;
    constexpr std::array<const char *, 6> geo_targets{"cx", "cy", "ox", "oy", "rz", "zoom"};
    constexpr std::array<const char *, 6> tf_targets{"cx", "cy", "x", "y", "rz", "zoom"};

    for (std::size_t i = 0; i < 6; ++i) {
        lua_getfield(L, -1, geo_targets[i]);
        lua_getfield(L, -2, "getvalue");
        lua_pushstring(L, tf_targets[i]);
        lua_call(L, 1, 1);
        input.geo_curr[i] = static_cast<float>(lua_tonumber(L, -2));
        input.tf_curr[i] = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 2);
    }

    if (ext && !input.frame) {
        switch (ext) {
            case 1:
                for (std::size_t i = 0; i < 6; ++i) {
                    const float v0 = get_val(tf_targets[i], 0);
                    const float v1 = get_val(tf_targets[i], dt);
                    input.tf_prev[i] = v0 * 2.0f - v1;
                }
                break;
            case 2: {
                const double dt2 = dt * 2.0;
                for (std::size_t i = 0; i < 6; ++i) {
                    const float v0 = get_val(tf_targets[i], 0);
                    const float v1 = get_val(tf_targets[i], dt);
                    const float v2 = get_val(tf_targets[i], dt2);
                    input.tf_prev[i] = v0 * 3.0f - v1 * 3.0f + v2;
                }
            } break;
            default:
                break;
        }
    } else if (input.frame) {
        lua_getfield(L, -1, "time");
        const double prev = lua_tonumber(L, -1) - dt;
        lua_pop(L, 1);

        for (std::size_t i = 0; i < 6; ++i) input.tf_prev[i] = get_val(tf_targets[i], prev);
    }

    input.pivot = Vec2<float>(input.tf_curr[0] + input.geo_curr[0], input.tf_curr[1] + input.geo_curr[1]);

    lua_pop(L, 1);
    return input;
}

Vec2<float>
Obj::resize(const std::array<int, 4> &margin) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj_ref);
    lua_getfield(L, -1, "effect");
    lua_pushstring(L, "領域拡張");
    lua_pushstring(L, "上");
    lua_pushinteger(L, margin[0]);
    lua_pushstring(L, "下");
    lua_pushinteger(L, margin[1]);
    lua_pushstring(L, "左");
    lua_pushinteger(L, margin[2]);
    lua_pushstring(L, "右");
    lua_pushinteger(L, margin[3]);
    lua_call(L, 9, 0);

    lua_getfield(L, -1, "cx");
    lua_getfield(L, -2, "cy");
    double cx = lua_tonumber(L, -2);
    double cy = lua_tonumber(L, -1);
    lua_pop(L, 2);
    cx += (margin[2] - margin[3]) * 0.5;
    cy += (margin[0] - margin[1]) * 0.5;
    lua_pushnumber(L, cx);
    lua_setfield(L, -2, "cx");
    lua_pushnumber(L, cy);
    lua_setfield(L, -2, "cy");

    lua_pop(L, 1);
    return Vec2<float>(static_cast<float>(cx), static_cast<float>(cy));
}

void
Obj::pixel_shader(const std::vector<float> &constants) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, obj_ref);
    lua_getfield(L, -1, "pixelshader");
    lua_pushstring(L, name.c_str());
    lua_pushstring(L, "object");
    lua_pushstring(L, "object");

    lua_newtable(L);
    for (int i = 0; i < constants.size(); ++i) {
        lua_pushnumber(L, constants[i]);
        lua_rawseti(L, -2, i + 1);
    }

    lua_call(L, 4, 0);
    lua_pop(L, 1);
}
