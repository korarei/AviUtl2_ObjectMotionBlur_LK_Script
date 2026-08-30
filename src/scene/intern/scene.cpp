#include "../scene.hpp"

#include <algorithm>
#include <cstdint>  // IWYU pragma: keep
#include <format>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4201)  // 非標準の無名構造体 (filter2.h FILTER_ITEM_COLOR)
#include <filter2.h>
#pragma warning(pop)

#include <intern/aviutl/aviutl.hpp>
#include <intern/string.hpp>

#include "instance.hpp"
#include "render.hpp"

namespace {
namespace aul = blur::aviutl;
namespace string = blur::string;
namespace renderer = blur::scene::renderer;

using Instance = blur::scene::Instance;

constexpr float kEpsilon = 1.0e-5f;

namespace properties {
namespace shutter {
FILTER_ITEM_GROUP name(L"Shutter", true);
FILTER_ITEM_TRACK angle(L"Shutter::Angle", 180.0, 0.0, 720.0, 0.01);
namespace falloff {
FILTER_ITEM_SEPARATOR name(L"Falloff");
namespace edge {
FILTER_ITEM_SELECT::ITEM contents[] = {
    {L"Trailing", 0},
    {L"Leading", 1},
    {L"Symmetric", 2},
    {nullptr, -1},
};
FILTER_ITEM_SELECT control(L"Shutter::Falloff::Edge", 2, contents);
auto& value = control.value;
}  // namespace edge
FILTER_ITEM_TRACK amount(L"Shutter::Falloff::Amount", 2.0, 0.0, 100.0, 0.01);
}  // namespace falloff
}  // namespace shutter
namespace sampling {
FILTER_ITEM_GROUP name(L"Sampling", false);
namespace viewport {
FILTER_ITEM_SEPARATOR name(L"Viewport");
FILTER_ITEM_TRACK sample_limit(L"Sampling::Viewport::Sample Limit", 128.0, 2.0, 4096.0, 1.0);
}  // namespace viewport
namespace render {
FILTER_ITEM_SEPARATOR name(L"Render");
FILTER_ITEM_TRACK sample_limit(L"Sampling::Render::Sample Limit", 512.0, 2.0, 4096.0, 1.0);
}  // namespace render
}  // namespace sampling
namespace compositing {
FILTER_ITEM_GROUP name(L"Compositing", false);
FILTER_ITEM_TRACK mix(L"Compositing::Mix", 100.0, 0.0, 100.0, 0.01);
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
    {L"Processed", 0}, {L"Flow", 1}, {L"Nearest Propagated Flow", 2}, {L"Distinct Propagated Flow", 3}, {nullptr, -1},
};
FILTER_ITEM_SELECT control(L"View", 0, contents);
auto& value = control.value;
}  // namespace view
}  // namespace properties

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

    const renderer::ID id{
        .w = static_cast<uint16_t>(ctx->object->width),
        .h = static_cast<uint16_t>(ctx->object->height),
        .preset = props::preset::value,
    };

    float scale = 0.0f;
    bool should_use_temporal_hints = false;

    bool is_boundary = !instance->curr.frame.has_value() || ctx->object->origin_frame == ctx->object->frame_s ||
                       section != instance->section;

    if (instance->id != id) {
        renderer::Remove(instance->id);
        instance->id = id;
        const size_t size = static_cast<size_t>(instance->id.w) * instance->id.h;
        instance->prev.image.resize(size);
        instance->curr.image.resize(size);
        renderer::Add(id);
        is_boundary = true;
    }

    if (is_boundary) {
        if (!ctx->copy_image_resource(L"resource:target", nullptr)) {
            aul::logger::Error(L"Failed to copy image 'object' to image 'resource:target'");
            return false;
        }

        ctx->get_image_data(instance->curr.image.data());
        instance->curr.frame = ctx->object->frame;
        instance->prev.frame = std::nullopt;
        instance->section = section;
    } else {
        if (instance->curr.frame != ctx->object->frame) {
            std::swap(instance->prev, instance->curr);
            ctx->get_image_data(instance->curr.image.data());
            instance->curr.frame = ctx->object->frame;
            instance->section = section;
        }

        if (instance->prev.frame.has_value()) {
            const auto df = ctx->object->frame - *instance->prev.frame;
            if (df == 1) {
                scale = 1.0f;
                should_use_temporal_hints = true;
            } else if (df != 0) {
                scale = 1.0f / static_cast<float>(df);
            }

            ctx->create_image_resource(L"resource:target", instance->prev.image.data(), ctx->object->width,
                                       ctx->object->height);
        } else {
            ctx->copy_image_resource(L"resource:target", nullptr);
        }
    }

    auto* const dst = ctx->get_image_texture2d();

    renderer::Sequence sequence{
        .inputs =
            {
                ctx->get_image_resource_texture2d(L"resource:reference"),
                ctx->get_image_resource_texture2d(L"resource:target"),
            },
        .depth = ctx->get_image_resource_texture2d(L"resource:depth"),
    };

    if (dst == nullptr || sequence.inputs[0uz] == nullptr || sequence.inputs[1uz] == nullptr ||
        sequence.depth == nullptr) {
        aul::logger::Error(L"Failed to get 'ID3D11Texture2D' pointers");
        return false;
    }

    const float amount = std::max(angle / 360.0f, 0.0f);

    const int32_t sample_limit = aul::context::GetEditorState() == aul::context::EditorState::kExporting
                                     ? static_cast<int32_t>(props::sampling::render::sample_limit.value)
                                     : static_cast<int32_t>(props::sampling::viewport::sample_limit.value);

    const renderer::Parameter param{
        .id = id,
        .should_use_temporal_hints = should_use_temporal_hints,
        .scale = scale * amount,
        .falloff_edge = props::shutter::falloff::edge::value,
        .falloff_amount = std::clamp(static_cast<float>(props::shutter::falloff::amount.value) * 0.01f, 0.0f, 1.0f),
        .sample_limit = sample_limit,
        .mix = std::clamp(static_cast<float>(props::compositing::mix.value) * 0.01f, 0.0f, 1.0f),
        .view_mode = props::view::value,
    };

    const auto ec =
        renderer::Render(dst, [&sequence, &param](const renderer::Context& ctx) { return ctx.Draw(sequence, param); });

    if (ec != std::error_code{}) {
        aul::logger::Error(ec.message());
        return false;
    }

    return true;
}

void* Init([[maybe_unused]] int64_t id) { return new Instance{}; }

void Deinit([[maybe_unused]] int64_t id, void* userdata) {
    auto* const instance = static_cast<Instance*>(userdata);
    renderer::Remove(instance->id);
    delete instance;
}

constinit void* props[] = {
    &properties::shutter::name,
    &properties::shutter::angle,
    &properties::shutter::falloff::name,
    &properties::shutter::falloff::edge::control,
    &properties::shutter::falloff::amount,
    &properties::sampling::name,
    &properties::sampling::viewport::name,
    &properties::sampling::viewport::sample_limit,
    &properties::sampling::render::name,
    &properties::sampling::render::sample_limit,
    &properties::compositing::name,
    &properties::compositing::mix,
    &properties::depth::name,
    &properties::depth::layer,
    &properties::additional_options,
    &properties::preset::control,
    &properties::layer_reference::control,
    &properties::view::control,
    nullptr,
};

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

    host->register_clear_cache_handler([]([[maybe_unused]] EDIT_SECTION* edit) { renderer::Reset(); });
}

void Unregister() { renderer::Deinit(); }
}  // namespace blur::scene
