#include <algorithm>
#include <array>
#include <iterator>
#include <string>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <logger2.h>
#include <module2.h>

#include "geo_utils.hpp"
#include "structs.hpp"
#include "transform_utils.hpp"

#ifndef VERSION
#define VERSION L"0.1.0"
#endif

static auto geo_map = GeoMap<8>();
static LOG_HANDLE *logger;

static void
extrapolate(const Param &param, const Context &context, Input &input) noexcept {
    if (!(param.ext && param.geo_cache)) {
        input.geo.prev = input.geo.curr;
        return;
    }

    bool flag = true;
    std::array<const Geo *, 2> geos{};

    for (int i = 0; i < param.ext; ++i) {
        if (auto g = geo_map.read(context.id, context.idx, i + 1))
            geos[i] = g;
        else
            flag = false;
    }

    if (flag) {
        switch (param.ext) {
            case 1:
                input.write_data(*input.geo.curr * 2.0 - *geos[0]);
                return;
            case 2:
                input.write_data(*input.geo.curr * 3.0 - *geos[0] * 3.0 + *geos[1]);
                return;
        }
    } else if (!input.geo.prev->is_valid()) {
        input.geo.prev = input.geo.curr;
    }
}

static void
read_geo(const Param &param, const Context &context, Input &input, int pos) noexcept {
    if (auto geo_prev = param.geo_cache ? geo_map.read(context.id, context.idx, pos) : nullptr)
        input.geo.prev = geo_prev;
    else
        input.geo.prev = input.geo.curr;
}

static std::array<Vec2<double>, 2>
calc_size(Delta &delta, double amt, const Context &context) noexcept {
    std::array<Vec2<double>, 2> margin{};

    const double h_amt = amt * 0.5;
    std::array<Mat3<double>, 2> htm_data{delta.calc_htm(h_amt), delta.calc_htm(amt)};
    std::array<Vec2<double>, 2> drift_data{delta.calc_drift(h_amt), delta.calc_drift(amt)};

    for (std::size_t i = 0; i < 2; ++i) {
        auto c_prev = Vec3<double>(-context.pivot + drift_data[i], 1.0);
        auto pos = (htm_data[i] * c_prev).to_vec2() + context.pivot;
        auto bbox = (htm_data[i].to_mat2().abs()) * context.res;

        auto diff = (bbox - context.res) * 0.5;
        margin[0] = margin[0].max((diff - pos).ceil());
        margin[1] = margin[1].max((diff + pos).ceil());
    }

    return margin;
}

static void
cleanup_geo(const Param &param, const Context &context) {
    if (context.idx != context.num - 1)
        return;

    switch (param.cache_ctrl) {
        case 1:
            if (context.frame != context.total_frame - 1)
                geo_map.clear(context.id);
            return;
        case 2:
            geo_map.clear();
            return;
        case 3:
            geo_map.clear(context.id);
            return;
        default:
            return;
    }
}

void
compute_motion(SCRIPT_MODULE_PARAM *p) {
    enum class Index : int {
        Param = 0,
        Context,
        GeoData,
        GeoCurr,
        XformCurr,
        XformPrev,
        Count
    };

    int n = p->get_param_num();
    if (n != static_cast<int>(Index::Count)) {
        p->set_error("Incorrect number of arguments");
        return;
    }

    auto to_num = [&](Index idx, const char *key) { return p->get_param_table_double(static_cast<int>(idx), key); };
    auto to_int = [&](Index idx, const char *key) { return p->get_param_table_int(static_cast<int>(idx), key); };

    const Param param{to_num(Index::Param, "amt"), to_int(Index::Param, "smp_lim"), to_int(Index::Param, "ext"),
                      to_int(Index::Param, "geo_cache"), to_int(Index::Param, "cache_ctrl")};

    const Context context{Vec2<double>(to_num(Index::Context, "w"), to_num(Index::Context, "h")),
                          Vec2<double>(to_num(Index::Context, "cx"), to_num(Index::Context, "cy")),
                          to_int(Index::Context, "id"),
                          to_int(Index::Context, "idx"),
                          to_int(Index::Context, "num"),
                          to_int(Index::Context, "frame"),
                          to_int(Index::Context, "total_farme")};

    Input input(
            reinterpret_cast<Geo *>(p->get_param_data(static_cast<int>(Index::GeoData))),
            Geo(to_num(Index::GeoCurr, "cx"), to_num(Index::GeoCurr, "cy"), to_num(Index::GeoCurr, "ox"),
                to_num(Index::GeoCurr, "oy"), to_num(Index::GeoCurr, "rz"), to_num(Index::GeoCurr, "zoom"),
                context.frame),
            Transform(to_num(Index::XformCurr, "cx"), to_num(Index::XformCurr, "cy"), to_num(Index::XformCurr, "x"),
                      to_num(Index::XformCurr, "y"), to_num(Index::XformCurr, "rz"), to_num(Index::XformCurr, "zoom")),
            Transform(to_num(Index::XformPrev, "cx"), to_num(Index::XformPrev, "cy"), to_num(Index::XformPrev, "x"),
                      to_num(Index::XformPrev, "y"), to_num(Index::XformPrev, "rz"), to_num(Index::XformPrev, "zoom")));

    const bool save_ed = param.geo_cache == 2;
    const bool save_st = param.geo_cache == 1 || (save_ed && (context.frame == 1 || context.frame == 2));
    int req_smp = 0;
    int smp = 0;
    Mat3<double> htm_base{};
    Vec2<double> drift{};
    std::array<Vec2<double>, 2> margin{};

    geo_map.resize(context.id, context.idx, context.num, param.geo_cache);

    if (save_st)
        geo_map.overwrite(context.id, context.idx, context.frame, *input.geo.curr);

    if (context.frame)
        read_geo(param, context, input, save_ed ? 0 : context.frame - 1);
    else
        extrapolate(param, context, input);

    auto delta = input.calc_delta();

    if (delta.is_moved()) {
        margin = calc_size(delta, param.amt, context);
        req_smp = static_cast<int>(std::ceil((margin[0] + margin[1]).norm(2)));
        smp = std::min(req_smp, param.smp_lim - 1);
    }

    if (smp) {
        htm_base = delta.calc_htm(param.amt, smp, true);
        drift = delta.calc_drift(param.amt, smp);
    }

    if (save_ed)
        geo_map.write(context.id, context.idx, 0, *input.geo.curr);

    cleanup_geo(param, context);

    LPCSTR drift_keys[] = {"x", "y"};
    LPCSTR margin_keys[] = {"left", "top", "right", "bottom"};

    p->push_result_int(req_smp + 1);
    p->push_result_int(smp);
    p->push_result_array_double(htm_base.ptr(), static_cast<int>(htm_base.size()));
    p->push_result_table_double(drift_keys, drift.ptr(), std::size(drift_keys));
    p->push_result_table_double(margin_keys, margin.data()->ptr(), std::size(margin_keys));

    auto mes = std::to_wstring(sizeof(Geo));
    logger->log(logger, mes.c_str());
}

static SCRIPT_MODULE_FUNCTION functions[] = {{L"compute_motion", compute_motion}, {nullptr}};

static SCRIPT_MODULE_TABLE script_module_table = {L"ObjectMotionBlur_LK v" VERSION L" by Korarei", functions};

extern "C" SCRIPT_MODULE_TABLE *
GetScriptModuleTable() {
    return &script_module_table;
}

extern "C" bool
InitializePlugin(DWORD v) {
    if (v < 2001900)
        return false;
    else
        return true;
}

extern "C" void
InitializeLogger(LOG_HANDLE *l) {
    logger = l;
}
