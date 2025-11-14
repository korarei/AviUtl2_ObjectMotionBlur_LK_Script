#include <algorithm>
#include <array>
#include <format>
#include <string>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <logger2.h>
#include <module2.h>

#include "geo.hpp"
#include "structs.hpp"
#include "transform.hpp"

#ifndef VERSION
#define VERSION L"0.1.0"
#endif

static auto geo_map = GeoMap<8>();
static LOG_HANDLE *logger;

static void
extrapolate(const Param &param, const Context &context, Flow &flow) noexcept {
    if (!(param.ext && param.geo_cache)) {
        flow.geo.prev = flow.geo.curr;
        return;
    }

    bool valid = true;
    std::array<const Geo *, 2> geos{};

    for (int i = 0; i < param.ext; ++i) {
        if (auto g = geo_map.read(context.id, context.idx, i + 1))
            geos[i] = g;
        else
            valid = false;
    }

    if (valid) {
        switch (param.ext) {
            case 1:
                flow.write_data(*flow.geo.curr * 2.0 - *geos[0]);
                return;
            case 2:
                flow.write_data(*flow.geo.curr * 3.0 - *geos[0] * 3.0 + *geos[1]);
                return;
        }
    } else if (!flow.geo.prev->is_valid()) {
        flow.geo.prev = flow.geo.curr;
    }
}

static void
read_geo(const Param &param, const Context &context, Flow &flow, int pos) noexcept {
    if (auto geo_prev = param.geo_cache ? geo_map.read(context.id, context.idx, pos) : nullptr)
        flow.geo.prev = geo_prev;
    else
        flow.geo.prev = flow.geo.curr;
}

static Mat2<double>
resize(const Context &context, const Delta &delta, double amt) noexcept {
    Mat2<double> margin{};

    std::array<Delta::Motion, 2> data{delta.compute_motion(amt * 0.5), delta.compute_motion(amt)};

    for (int i = 0; i < 2; ++i) {
        auto c_prev = Vec3<double>(-context.pivot + data[i].drift, 1.0);
        auto pos = (data[i].htm * c_prev).to_vec2() + context.pivot;
        auto bbox = (data[i].htm.to_mat2().abs()) * context.res;

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
            if (context.frame != context.range - 1)
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

static Param
load_param(SCRIPT_MODULE_PARAM *p, int idx) {
    auto to_num = [&](const char *key) { return p->get_param_table_double(idx, key); };
    auto to_int = [&](const char *key) { return p->get_param_table_int(idx, key); };
    auto to_bool = [&](const char *key) { return p->get_param_table_boolean(idx, key); };

    return {to_num("amt"),       to_int("smp_lim"),    to_int("ext"),
            to_int("geo_cache"), to_int("cache_ctrl"), to_bool("print_info")};
}

static Context
load_context(SCRIPT_MODULE_PARAM *p, int idx) {
    auto to_num = [&](const char *key) { return p->get_param_table_double(idx, key); };
    auto to_int = [&](const char *key) { return p->get_param_table_int(idx, key); };

    return Context(to_num("w"), to_num("h"), to_num("cx"), to_num("cy"), to_int("id"), to_int("idx"), to_int("num"),
                   to_int("frame"), to_int("range"));
}

static Flow
load_flow(SCRIPT_MODULE_PARAM *p, int idx) {
    auto to_num = [&](const char *key, int ofs = 0) { return p->get_param_table_double(idx + ofs, key); };
    auto to_int = [&](const char *key, int ofs = 0) { return p->get_param_table_int(idx + ofs, key); };
    auto to_data = [&]() { return reinterpret_cast<Geo *>(p->get_param_data(idx)); };

    return Flow(to_data(),
                Geo(to_num("cx", 1), to_num("cy", 1), to_num("ox", 1), to_num("oy", 1), to_num("rz", 1),
                    to_num("zoom", 1), to_int("frame", 1)),
                Transform(to_num("cx", 2), to_num("cy", 2), to_num("x", 2), to_num("y", 2), to_num("rz", 2),
                          to_num("zoom", 2)),
                Transform(to_num("cx", 3), to_num("cy", 3), to_num("x", 3), to_num("y", 3), to_num("rz", 3),
                          to_num("zoom", 3)));
}

void
compute_motion(SCRIPT_MODULE_PARAM *p) {
    const int n = p->get_param_num();
    if (n != 6) {
        p->set_error("Incorrect number of arguments");
        return;
    }

    const Param param = load_param(p, 0);
    const Context context = load_context(p, 1);
    Flow flow = load_flow(p, 2);

    const bool save_ed = param.geo_cache == 2;
    const bool save_st = param.geo_cache == 1 || (save_ed && (context.frame == 1 || context.frame == 2));

    int req_smp = 0;
    int smp = 0;
    Delta::Motion motion{};
    Mat2<double> margin{};

    geo_map.resize(context.id, context.idx, context.num, param.geo_cache);

    if (save_st)
        geo_map.overwrite(context.id, context.idx, context.frame, *flow.geo.curr);

    if (context.frame)
        read_geo(param, context, flow, save_ed ? 0 : context.frame - 1);
    else
        extrapolate(param, context, flow);

    const auto delta = flow.delta();

    if (delta.is_moved()) {
        margin = resize(context, delta, param.amt);
        req_smp = static_cast<int>(std::ceil((margin[0] + margin[1]).norm(2)));
        smp = std::min(req_smp, param.smp_lim - 1);
    }

    if (smp)
        motion = delta.compute_motion(param.amt, smp, true);

    if (save_ed)
        geo_map.write(context.id, context.idx, 0, *flow.geo.curr);

    cleanup_geo(param, context);

    if (param.print_info) {
        std::wstring info = std::format(
                L"\n"
                L"Object ID       : {}\n"
                L"Index           : {}\n"
                L"Required Samples: {}",
                context.id, context.idx, req_smp + 1);

        std::wstring verbose = std::format(
                L"\n"
                L"Size of Geo class: {} bytes",
                sizeof(Geo));

        logger->info(logger, info.c_str());
        logger->verbose(logger, verbose.c_str());
    }

    LPCSTR keys[] = {"left", "top", "right", "bottom"};
    p->push_result_int(smp);
    p->push_result_array_double(motion.htm.ptr(), static_cast<int>(motion.htm.size()));
    p->push_result_array_double(motion.drift.ptr(), static_cast<int>(motion.drift.size()));
    p->push_result_table_double(keys, margin.ptr(), static_cast<int>(margin.size()));
}

static SCRIPT_MODULE_FUNCTION functions[] = {{L"compute_motion", compute_motion}, {nullptr}};

static SCRIPT_MODULE_TABLE script_module_table = {L"ObjectMotionBlur_LK v" VERSION L" by Korarei", functions};

extern "C" SCRIPT_MODULE_TABLE *
GetScriptModuleTable() {
    return &script_module_table;
}

extern "C" void
InitializeLogger(LOG_HANDLE *l) {
    logger = l;
}

extern "C" bool
InitializePlugin(DWORD v) {
    if (v < 2001901)
        return false;
    else
        return true;
}
