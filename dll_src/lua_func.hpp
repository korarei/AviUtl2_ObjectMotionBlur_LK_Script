#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <lua.hpp>

#include "structs.hpp"
#include "transform_utils.hpp"
#include "vector_2d.hpp"

class Obj {
public:
    Obj(lua_State *L);
    ~Obj();

    [[nodiscard]] Param get_param();

    [[nodiscard]] Input get_input(std::uint32_t ext);

    [[nodiscard]] bool get_saving();

    [[nodiscard]] void resize(const std::array<Vec2<float>, 2> &margin, const Vec2<float> &center);

    void pixel_shader(const std::string &name, const std::vector<float> &constants);

    void print(const std::string &str);

private:
    lua_State *L;
    int obj_ref;

    [[nodiscard]] const char *get_string(int idx, const char *d);
    [[nodiscard]] lua_Number get_number(int idx, lua_Number d);
    [[nodiscard]] lua_Integer get_integer(int idx, lua_Integer d);
    [[nodiscard]] bool get_boolean(int idx, bool d);

    [[nodiscard]] float get_val(const char *target, double time);
};

inline const char *
Obj::get_string(int idx, const char *d) {
    return lua_isstring(L, idx) ? lua_tostring(L, idx) : d;
}

inline double
Obj::get_number(int idx, lua_Number d) {
    return lua_isnumber(L, idx) ? lua_tonumber(L, idx) : d;
}

inline lua_Integer
Obj::get_integer(int idx, lua_Integer d) {
    return lua_isnumber(L, idx) ? lua_tointeger(L, idx) : d;
}

inline bool
Obj::get_boolean(int idx, bool d) {
    return lua_isboolean(L, idx) ? lua_toboolean(L, idx) : d;
}
