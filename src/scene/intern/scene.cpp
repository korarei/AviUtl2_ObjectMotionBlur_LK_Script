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
#include "instance.hpp"

namespace {
namespace aul = blur::aviutl;
namespace string = blur::string;

using Instance = blur::scene::Instance;
using Renderer = blur::scene::Renderer;

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
    {L"Propagated Flow", 5},
    {L"Propagated Quality", 6},
    {nullptr, -1},
};
FILTER_ITEM_SELECT control(L"View", 0, contents);
auto& value = control.value;
}  // namespace view
}  // namespace properties

Renderer renderer{};

bool Apply(FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    if (ctx->object->width <= 0 || ctx->object->height <= 0) {
        return false;
    }

    if (ctx->object->num != 1) {
        aul::logger::Error(L"This effect only supports a single object");
        return false;
    }

    const float angle = static_cast<float>(props::shutter::angle.value);

    if (angle <= kEpsilon) {
        return true;
    }

    const auto frame = ctx->object->frame_s + ctx->object->frame;
    auto* const instance = static_cast<Instance*>(ctx->userdata);

    int section = 0;

    {
        auto* const object = ctx->edit->find_object(ctx->object->effect_layer, frame);

        if (object == nullptr) {
            aul::logger::Error(
                std::format(L"No object exists at layer {}, frame {}", ctx->object->effect_layer + 1, frame));
            return false;
        }

        {
            const auto n = ctx->edit->get_object_section_num(object);
            while (section < n && ctx->edit->get_object_section_frame(object, section) <= frame) {
                ++section;
            }
        }
    }

    const bool is_boundary = frame == ctx->object->frame_s || section != instance->section ||
                             instance->image.w != ctx->object->width || instance->image.h != ctx->object->height;

    if (!ctx->copy_image_resource(L"resource:reference", nullptr)) {
        aul::logger::Error(L"Failed to copy image 'object' to image 'resource:reference'");
        return false;
    }

    {
        int depth_layer = static_cast<int>(props::depth::layer.value);
        if (props::layer_reference::value == 1) {
            depth_layer += ctx->object->effect_layer + 1;
        }

        --depth_layer;

        if (depth_layer < 0 || depth_layer == ctx->object->layer) {
            constexpr PIXEL_RGBA opaque{255, 255, 255, 255};
            ctx->create_image_resource(L"resource:depth", &opaque, 1, 1);
        } else if (ctx->get_image_object(depth_layer, 0.0) == nullptr) {
            aul::logger::Error(std::format(L"No object exists at layer {}, frame {}", depth_layer + 1, frame));
            return false;
        } else {
            const auto src = std::format(L"layer:{}+", depth_layer);

            if (!ctx->copy_image_resource(L"resource:depth", src.c_str())) {
                aul::logger::Error(std::format(L"Failed to copy image '{}' to image 'resource:depth'", src));
                return false;
            }
        }
    }

    float scale = 0.0f;
    bool should_use_temporal_hints = false;

    if (is_boundary) {
        if (!ctx->copy_image_resource(L"resource:target", nullptr)) {
            aul::logger::Error(L"Failed to copy image 'object' to image 'resource:target'");
            return false;
        }

        instance->image.w = ctx->object->width, instance->image.h = ctx->object->height;
        instance->image.data.resize(static_cast<size_t>(instance->image.w) * instance->image.h);
    } else {
        ctx->create_image_resource(L"resource:target", instance->image.data.data(), instance->image.w,
                                   instance->image.h);

        if (const auto df = ctx->object->frame - instance->frame; df != 0) {
            scale = 1.0f / static_cast<float>(df);
            should_use_temporal_hints = true;
        }
    }

    ctx->get_image_data(instance->image.data.data());
    instance->section = section;
    instance->frame = ctx->object->frame;

    auto* const dst = ctx->get_image_texture2d();

    Renderer::Source src{
        .inputs =
            {
                ctx->get_image_resource_texture2d(L"resource:reference"),
                ctx->get_image_resource_texture2d(L"resource:target"),
            },
        .depth = ctx->get_image_resource_texture2d(L"resource:depth"),
    };

    if (dst == nullptr || src.inputs[0uz] == nullptr || src.inputs[1uz] == nullptr || src.depth == nullptr) {
        aul::logger::Error(L"Failed to get 'ID3D11Texture2D' pointers");
        return false;
    }

    const float amount = std::max(angle / 360.0f, 0.0f);

    const int32_t sample_limit = aul::context::CurrentEditorState() == aul::context::EditorState::kExporting
                                     ? static_cast<int32_t>(props::sampling::render::sample_limit.value)
                                     : static_cast<int32_t>(props::sampling::viewport::sample_limit.value);

    const Renderer::Param param{
        .id =
            {
                .w = static_cast<uint16_t>(ctx->object->width),
                .h = static_cast<uint16_t>(ctx->object->height),
                .preset = props::preset::value,
            },
        .should_use_temporal_hints = should_use_temporal_hints,
        .scale = scale,
        .amount = amount,
        .phase = 0.0f,
        .sample_limit = sample_limit,
        .mix = std::clamp(static_cast<float>(props::compositing::mix.value) * 0.01f, 0.0f, 1.0f),
        .falloff = std::max(static_cast<float>(props::compositing::falloff.value) * 0.01f, 0.0f),
        .view_mode = props::view::value,
    };

    const auto ec = renderer.Render(
        dst, [&src, &param](Renderer::Context& ctx) -> std::error_code { return ctx.Draw(src, param); });

    if (ec != std::error_code{}) {
        aul::logger::Error(string::ToWString(string::AsUTF8(ec.message())));
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

void* Init([[maybe_unused]] int64_t id) { return new Instance{}; }

void Deinit([[maybe_unused]] int64_t id, void* instance) { delete static_cast<Instance*>(instance); }

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
