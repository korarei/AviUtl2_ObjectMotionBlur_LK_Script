#include "../scene.hpp"

#include <algorithm>
#include <cstdint>  // IWYU pragma: keep
#include <format>

#pragma warning(push)
#pragma warning(disable : 4201)  // 非標準の無名構造体 (filter2.h FILTER_ITEM_COLOR)
#include <filter2.h>
#pragma warning(pop)

#include <intern/aviutl/aviutl.hpp>
#include <intern/string.hpp>

#include "direct3d.hpp"

namespace {
namespace aul = blur::aviutl;
namespace d3d = blur::scene::direct3d;
namespace string = blur::string;

constexpr float kEpsilon = 1.0e-5f;

namespace properties {
namespace shutter {
FILTER_ITEM_GROUP name(L"Shutter", true);
FILTER_ITEM_TRACK angle(L"Shutter::Angle", 180.0, 0.0, 720.0, 0.01);
}  // namespace shutter
namespace sampling {
FILTER_ITEM_GROUP name(L"Sampling", false);
namespace viewport {
FILTER_ITEM_SEPARATOR name(L"Viewport");
FILTER_ITEM_TRACK sample_limit(L"Sampling::Viewport::Sample Limit", 8.0, 2.0, 128.0, 1.0);
}  // namespace viewport
namespace render {
FILTER_ITEM_SEPARATOR name(L"Render");
FILTER_ITEM_TRACK sample_limit(L"Sampling::Render::Sample Limit", 32.0, 2.0, 128.0, 1.0);
}  // namespace render
}  // namespace sampling
namespace compositing {
FILTER_ITEM_GROUP name(L"Compositing", false);
FILTER_ITEM_TRACK mix(L"Compositing::Mix", 100.0, 0.0, 100.0, 0.01);
FILTER_ITEM_TRACK falloff(L"Compositing::Falloff", 0.0, 0.0, 100.0, 0.01);
}  // namespace compositing
namespace depth {
FILTER_ITEM_GROUP name(L"Depth", false);
FILTER_ITEM_TRACK layer(L"Depth::Layer", 0.0, -100.0, 100.0, 1.0, L"---");
}  // namespace depth
FILTER_ITEM_GROUP additional_options(L"Additional Options", false);
namespace preset {
FILTER_ITEM_SELECT::ITEM contents[] = {
    {L"Slow", 0},
    {L"Medium", 1},
    {L"Fast", 2},
    {nullptr, -1},
};
FILTER_ITEM_SELECT control(L"Preset", 0, contents);
auto& value = control.value;
}  // namespace preset
namespace layer_reference {
FILTER_ITEM_SELECT::ITEM contents[] = {
    {L"Absolute", 0},
    {L"Relative", 1},
    {nullptr, -1},
};
FILTER_ITEM_SELECT control(L"Layer Reference", 0, contents);
auto& value = control.value;
}  // namespace layer_reference
namespace view {
FILTER_ITEM_SELECT::ITEM contents[] = {
    {L"Processed", 0},
    {L"Flow", 1},
    {L"Cost", 2},
    {L"Forward-Backward Consistency", 3},
    {L"Depth", 4},
    {L"Dominant Tile Motion", 5},
    {L"Alternate Tile Motion", 6},
    {nullptr, -1},
};
FILTER_ITEM_SELECT control(L"View", 0, contents);
auto& value = control.value;
}  // namespace view
}  // namespace properties

d3d::Renderer renderer{};

bool Apply(FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    if (ctx->object->width <= 0 || ctx->object->height <= 0) {
        return false;
    }

    if (ctx->object->num != 1) {
        aul::Logger::Error(L"Scene Motion Blur is only for 1 object");
        return false;
    }

    const float angle = static_cast<float>(props::shutter::angle.value);
    if (angle <= kEpsilon) {
        return true;
    }

    const int frame = ctx->object->frame_s + ctx->object->frame;
    auto* const object = ctx->edit->find_object(ctx->object->effect_layer, frame);

    if (object == nullptr) {
        aul::Logger::Error(L"Failed to find object");
        return false;
    }

    int section = 0;

    {
        const auto n = ctx->edit->get_object_section_num(object);
        while (section < n && ctx->edit->get_object_section_frame(object, section) <= frame) {
            ++section;
        }
    }

    if (!ctx->copy_image_resource(L"resource:source", nullptr)) {
        aul::Logger::Error(L"Failed to copy image resource");
        return false;
    }

    {
        int depth_layer = static_cast<int>(props::depth::layer.value);
        if (props::layer_reference::value == 1) {
            depth_layer += ctx->object->layer + 1;
        }

        depth_layer -= 1;

        if (depth_layer < 0 || depth_layer == ctx->object->layer ||
            !ctx->copy_image_resource(L"resource:depth", std::format(L"layer:{}+", depth_layer).c_str())) {
            constexpr PIXEL_RGBA opaque{255, 255, 255, 255};
            ctx->create_image_resource(L"resource:depth", &opaque, 1, 1);
        }
    }

    auto* const dst = ctx->get_image_texture2d();
    auto* const src = ctx->get_image_resource_texture2d(L"resource:source");
    auto* const depth = ctx->get_image_resource_texture2d(L"resource:depth");

    if (src == nullptr || dst == nullptr || depth == nullptr) {
        aul::Logger::Error(L"Failed to get image resource");
        return false;
    }

    const float amount = std::max(angle / 360.0f, 0.0f);

    const int32_t sample_limit = aul::Context::CurrentSessionState() == aul::Context::SessionState::kRendering
                                     ? static_cast<int32_t>(props::sampling::render::sample_limit.value)
                                     : static_cast<int32_t>(props::sampling::viewport::sample_limit.value);

    const auto preset = props::preset::value == 0   ? NV_OF_PERF_LEVEL_SLOW
                        : props::preset::value == 1 ? NV_OF_PERF_LEVEL_MEDIUM
                                                    : NV_OF_PERF_LEVEL_FAST;

    const d3d::Renderer::Param param{
        .id = ctx->object->effect_id,
        .section = section,
        .frame = ctx->object->frame,
        .preset = preset,
        .amount = amount,
        .phase = 0.0f,
        .sample_limit = sample_limit,
        .mix = std::clamp(static_cast<float>(props::compositing::mix.value) * 0.01f, 0.0f, 1.0f),
        .falloff = std::max(static_cast<float>(props::compositing::falloff.value) * 0.01f, 0.0f),
        .view_mode = props::view::value,
    };

    const auto ec = renderer.Render(dst, [src, depth, &param](d3d::Renderer::Context& ctx) -> std::error_code {
        const auto flow = ctx.ComputeFlow(src, depth, param);
        if (!flow.has_value()) {
            return flow.error();
        }

        if (param.view_mode != 0) {
            aul::Logger::Log(
                std::format(L"\n"
                            L"Effect ID : {}\n"
                            L"Grid Size : {}x{}\n",
                            param.id, flow->grid_size, flow->grid_size));

            return ctx.Debug(*flow, param);
        }

        return ctx.Draw(*flow, param);
    });

    if (ec != std::error_code{}) {
        aul::Logger::Error(string::ToWString(string::AsUTF8(ec.message())));
        return false;
    }

    return true;
}

constinit void* props[] = {
    &properties::shutter::name,
    &properties::shutter::angle,
    &properties::sampling::name,
    &properties::sampling::viewport::name,
    &properties::sampling::viewport::sample_limit,
    &properties::sampling::render::name,
    &properties::sampling::render::sample_limit,
    &properties::compositing::name,
    &properties::compositing::mix,
    &properties::compositing::falloff,
    &properties::depth::name,
    &properties::depth::layer,
    &properties::additional_options,
    &properties::preset::control,
    &properties::layer_reference::control,
    &properties::view::control,
    nullptr,
};

void* Init([[maybe_unused]] int64_t id) { return nullptr; };

void Deinit(int64_t id, [[maybe_unused]] void* userdata) { renderer.Reset(id); };

constinit FILTER_PLUGIN_TABLE desc{
    .flag = FILTER_PLUGIN_TABLE::FLAG_VIDEO | FILTER_PLUGIN_TABLE::FLAG_FILTER | FILTER_PLUGIN_TABLE::FLAG_USERDATA,
    .name = L"SceneMotionBlur_K",
    .label = L"ぼかし",
    .information = L"SceneMotionBlur_K v" VERSION L" by Korarei",
    .items = props,
    .func_proc_video = Apply,
    .func_proc_audio = nullptr,
    .func_create = Init,
    .func_destroy = Deinit,
};
}  // namespace

namespace blur::scene {
void Register(HOST_APP_TABLE* host) {
    host->register_filter_plugin(&desc);

    host->register_clear_cache_handler([]([[maybe_unused]] EDIT_SECTION* edit) { renderer.Reset(); });
}

void Unregister() { renderer.Reset(); }
}  // namespace blur::scene
