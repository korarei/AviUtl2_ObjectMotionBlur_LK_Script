#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <unordered_map>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <logger2.h>
#include <module2.h>

#include "geo.hpp"
#include "structs.hpp"
#include "transform.hpp"
#include "vector/vector.hpp"

#ifndef VERSION
#define VERSION L"0.1.0"
#endif

using AtlasOct = Atlas<8>;

static auto atlas_table = std::unordered_map<std::string, AtlasOct>{};
static int ver = 0;
static LOG_HANDLE *logger;

static void
extrapolate(AtlasOct &atlas, const Param &param, const Context &context, Flow &flow) noexcept {
    if (!param.ext)
        return;

    bool valid = true;
    std::array<const Geo *, 2> geos{};

    for (int i = 0; i < param.ext; ++i) {
        if (auto g = atlas.read(context.id, context.idx, i + 2))
            geos[i] = g;
        else
            valid = false;
    }

    if (valid) {
        switch (param.ext) {
            case 1:
                atlas.overwrite(context.id, context.idx, 0, *flow.geo.curr * 2.0 - *geos[0]);
                break;
            case 2:
                atlas.overwrite(context.id, context.idx, 0, *flow.geo.curr * 3.0 - *geos[0] * 3.0 + *geos[1]);
                break;
            default:
                atlas.overwrite(context.id, context.idx, 0, *flow.geo.curr);
                break;
        }

        if (auto g = atlas.read(context.id, context.idx, 0)) {
            flow.geo.prev = g;
            flow.write_data(*g);
        }
    } else if (auto g = flow.read_data()) {
        flow.geo.prev = g;
    }
}

static Mat2<double>
resize(const Context &context, const Delta &delta, double amt) noexcept {
    Mat2<double> margin{};

    std::array<Delta::Motion, 2> data{delta.build_xform(amt * 0.5), delta.build_xform(amt)};
    for (int i = 0; i < 2; ++i) {
        const auto xform = data[i].xform * data[i].scale;
        auto c_prev = Vec3<double>(-context.pivot, 1.0) + data[i].drift;
        auto pos = (xform * c_prev).to_vec2() + context.pivot;
        auto bbox = (xform.to_mat2().abs()) * context.res;

        auto diff = (bbox - context.res) * 0.5;
        margin[0] = margin[0].max((diff - pos).ceil());
        margin[1] = margin[1].max((diff + pos).ceil());
    }

    return margin;
}

static void
cleanup_geo(AtlasOct &atlas, const Param &param, const Context &context) {
    if (context.idx != context.num - 1)
        return;

    switch (param.cache_purge) {
        case 1:
            if (context.frame != context.range - 1)
                atlas.clear(context.id);
            return;
        case 2:
            atlas.clear();
            return;
        case 3:
            atlas.clear(context.id);
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
    auto to_string = [&](const char *key) { return p->get_param_table_string(idx, key); };

    return Context(to_string("name"), to_num("w"), to_num("h"), to_num("cx"), to_num("cy"), to_int("id"), to_int("idx"),
                   to_int("num"), to_int("frame"), to_int("range"));
}

static Flow
load_flow(SCRIPT_MODULE_PARAM *p, int idx, int frame) {
    auto to_num = [&](const char *key, int ofs) { return p->get_param_table_double(idx + ofs, key); };
    auto to_int = [&](const char *key, int ofs) { return p->get_param_table_int(idx + ofs, key); };

    auto to_xform = [&](int ofs) {
        return Transform(to_num("cx", ofs), to_num("cy", ofs), to_num("x", ofs), to_num("y", ofs), to_num("rz", ofs),
                         to_num("sx", ofs), to_num("sy", ofs));
    };

    auto to_geo = [&](int ofs) {
        return Geo(frame, to_num("cx", ofs), to_num("cy", ofs), to_num("ox", ofs), to_num("oy", ofs), to_num("rz", ofs),
                   to_num("sx", ofs), to_num("sy", ofs));
    };

    auto to_data = [&](int ofs) {
        auto data = reinterpret_cast<Geo *>(p->get_param_data(idx + ofs));
        return data && p->get_param_int(idx + ofs + 1) == sizeof(Geo) ? data : nullptr;
    };

    return Flow(to_xform(0), to_xform(1), to_geo(2), to_data(3));
}

static void
compute_motion(SCRIPT_MODULE_PARAM *p) {
    const int n = p->get_param_num();
    if (n != 5 && n != 7) {
        p->set_error("Incorrect number of arguments");
        return;
    }

    const Param param = load_param(p, 0);
    const Context context = load_context(p, 1);
    Flow flow = load_flow(p, 2, context.frame);

    const bool save_ed = param.geo_cache == 2;
    const bool save_st = param.geo_cache == 1 || (save_ed && (context.frame == 1 || context.frame == 2));

    auto &atlas = atlas_table[context.name];
    int req_smp = 0;
    int smp = 0;
    Mat2<double> margin{};

    try {
        atlas.resize(context.id, context.idx, context.num, param.geo_cache);
        if (!param.geo_cache) {
            if (auto g = flow.read_data())
                flow.write_data(Geo());
        }
    } catch (...) {
        p->set_error("Initialization failed");
        return;
    }

    if (save_st)
        atlas.overwrite(context.id, context.idx, context.frame + 1, *flow.geo.curr);

    if (param.geo_cache) {
        if (!context.frame)
            extrapolate(atlas, param, context, flow);
        else if (auto g = atlas.read(context.id, context.idx, save_ed ? 1 : context.frame))
            flow.geo.prev = g;
    }

    const auto delta = flow.delta();

    if (delta.is_moved()) {
        margin = resize(context, delta, param.amt);
        req_smp = static_cast<int>(std::ceil((margin[0] + margin[1]).norm(2)));
        smp = std::min(req_smp, param.smp_lim - 1);
    }

    auto motion = delta.build_xform(param.amt, smp, true);

    if (save_ed)
        atlas.write(context.id, context.idx, 1, *flow.geo.curr);

    cleanup_geo(atlas, param, context);

    if (param.print_info) {
        std::wstring info = std::format(
                L"\n"
                L"Object ID       : {}\n"
                L"Index           : {}\n"
                L"Required Samples: {}",
                context.id, context.idx, req_smp + 1);

        std::wstring verbose = std::format(L"Size of Geo class: {} B", sizeof(Geo));

        logger->info(logger, info.c_str());
        logger->verbose(logger, verbose.c_str());
    }

    LPCSTR keys[] = {"left", "top", "right", "bottom"};
    p->push_result_table_double(keys, margin.data(), static_cast<int>(margin.size()));
    p->push_result_int(smp);
    p->push_result_array_double(motion.xform.data(), static_cast<int>(motion.xform.size()));
    p->push_result_array_double(motion.scale.matrix().data(), static_cast<int>(motion.scale.size()));
    p->push_result_array_double(motion.drift.data(), static_cast<int>(motion.drift.size()));
}

static void
version(SCRIPT_MODULE_PARAM *p) {
    p->push_result_int(ver);
}

static SCRIPT_MODULE_FUNCTION functions[] = {{L"compute_motion", compute_motion}, {L"version", version}, {nullptr}};

static SCRIPT_MODULE_TABLE script_module_table = {L"ObjectMotionBlur_LK v" VERSION L" by Korarei", functions};

extern "C" {
SCRIPT_MODULE_TABLE *
GetScriptModuleTable() {
    return &script_module_table;
}

void
InitializeLogger(LOG_HANDLE *l) {
    logger = l;
}

bool
InitializePlugin(DWORD v) {
    if (v < 2002000)
        return false;

    constexpr auto parse = [](const wchar_t *s) {
        int v[3] = {0, 0, 0}, idx = 0;

        for (int i = 0; s[i]; ++i) {
            if (L'0' <= s[i] && s[i] <= L'9')
                v[idx] = v[idx] * 10 + (s[i] - L'0');
            else if (s[i] == L'.' && idx < 2)
                ++idx;
        }

        return v[0] * 1000000 + v[1] * 1000 + v[2];
    };

    ver = parse(VERSION);

    return true;
}
}
