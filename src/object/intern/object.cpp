#include "../object.hpp"

#include <cstdint>  // IWYU pragma: keep

#pragma warning(push)
#pragma warning(disable : 4201)  // 非標準の無名構造体 (filter2.h FILTER_ITEM_COLOR)
#include <filter2.h>
#pragma warning(pop)

#include <intern/aviutl/aviutl.hpp>

namespace {
namespace aul = blur::aviutl;

void* props[] = {
    nullptr,
};

bool Apply(FILTER_PROC_VIDEO* ctx) { return true; }

constinit FILTER_PLUGIN_TABLE info = {
    .flag = FILTER_PLUGIN_TABLE::FLAG_VIDEO,
    .name = L"ObjectMotionBlur_LK",
    .label = L"ぼかし",
    .information = L"ObjectMotionBlur_LK v" VERSION L" by Korarei",
    .items = props,
    .func_proc_video = Apply,
    .func_proc_audio = nullptr,
};
}  // namespace

namespace blur::object {
void Init(HOST_APP_TABLE* host) { host->register_filter_plugin(&info); }
}  // namespace blur::object
