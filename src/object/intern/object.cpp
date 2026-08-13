#include "../object.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <vector>

#include <Eigen/Geometry>

#pragma warning(push)
#pragma warning(disable : 4201)  // 非標準の無名構造体 (filter2.h FILTER_ITEM_COLOR)
#include <filter2.h>
#pragma warning(pop)

#include <intern/aviutl/aviutl.hpp>
#include <intern/string.hpp>

#include "cache.hpp"
#include "direct3d.hpp"

namespace {
namespace aul = blur::aviutl;
namespace string = blur::string;
namespace cache = blur::object::cache;

using Renderer = blur::object::Renderer;

constexpr float kEpsilon = Eigen::NumTraits<float>::dummy_precision();

struct Object {
    struct Transform {
        Eigen::Vector2f position = Eigen::Vector2f::Zero();
        Eigen::Vector2f scale = Eigen::Vector2f::Ones();
        float rotation = 0.0f;
    };

    struct Snapshot {
        Eigen::Affine2f world_transform = Eigen::Affine2f::Identity();
        Eigen::Vector2f pivot = Eigen::Vector2f::Zero();
        std::vector<Transform> transforms;
    };

    struct State {
        Snapshot start{};
        Snapshot end{};
        size_t depth = 0u;
    };

    Eigen::Affine2f transform = Eigen::Affine2f::Identity();
    Eigen::Vector2f dimensions = Eigen::Vector2f::Zero();
    State state{};
};

namespace properties {
namespace shutter {
FILTER_ITEM_GROUP name(L"Shutter", true);
FILTER_ITEM_TRACK angle(L"Shutter::Angle", 180.0, 0.0, 720.0, 0.01);
FILTER_ITEM_TRACK phase(L"Shutter::Phase", -90.0, -360.0, 360.0, 0.01);
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
FILTER_ITEM_TRACK falloff(L"Compositing::Falloff", 0.0, 0.0, 100.0, 0.01);
}  // namespace compositing
FILTER_ITEM_GROUP additional_options(L"Additional Options", false);
namespace extrapolation {
FILTER_ITEM_SELECT::ITEM contents[] = {{L"None", 0}, {L"Linear", 1}, {L"Quadratic", 2}, {nullptr, -1}};
FILTER_ITEM_SELECT control(L"Extrapolation", 2, contents);
auto& value = control.value;
}  // namespace extrapolation
FILTER_ITEM_CHECK should_resize(L"Resize", true);
FILTER_ITEM_CHECK should_print_diagnostics(L"Diagnostics", false);
namespace internal {
struct Record {
    std::array<cache::Store::Transform, 2uz> transforms{};
    int num = 0;
};

static_assert(sizeof(std::array<Record, 16uz>) <= 1024uz);

FILTER_ITEM_DATA<std::array<Record, 16uz>> records0(L"Internal::Records[0]");
FILTER_ITEM_DATA<std::array<Record, 16uz>> records1(L"Internal::Records[1]");
FILTER_ITEM_DATA<std::array<Record, 16uz>> records2(L"Internal::Records[2]");
FILTER_ITEM_DATA<std::array<Record, 16uz>> records3(L"Internal::Records[3]");
FILTER_ITEM_DATA<std::array<Record, 16uz>> records4(L"Internal::Records[4]");
FILTER_ITEM_DATA<std::array<Record, 16uz>> records5(L"Internal::Records[5]");
FILTER_ITEM_DATA<std::array<Record, 16uz>> records6(L"Internal::Records[6]");
FILTER_ITEM_DATA<std::array<Record, 16uz>> records7(L"Internal::Records[7]");

constexpr std::array<FILTER_ITEM_DATA<std::array<Record, 16uz>>*, 8uz> kRecords = {
    &records0, &records1, &records2, &records3, &records4, &records5, &records6, &records7,
};
}  // namespace internal
}  // namespace properties

Renderer renderer{};
cache::Store store{};

[[nodiscard]] constexpr float ToRadians(float deg) noexcept {
    constexpr float f = std::numbers::pi_v<float> / 180.0f;
    return deg * f;
}

[[nodiscard]] inline Eigen::Vector2f lerp(const Eigen::Vector2f& a, const Eigen::Vector2f& b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] std::vector<EFFECT_HANDLE> GetEmptyHandles(int frame, const FILTER_PROC_VIDEO* ctx) {
    std::vector<EFFECT_HANDLE> handles{};
    handles.reserve(ctx->object->layer);

    {
        int layer = ctx->object->layer - 1;

        for (int i = layer; i >= 0; --i) {
            if (!ctx->edit->get_layer_enable(i)) {
                continue;
            }

            auto* const object_handle = ctx->edit->find_object(i, frame);

            if (object_handle == nullptr || ctx->edit->get_object_layer_frame(object_handle).start > frame) {
                continue;
            }

            auto* const candidate = ctx->edit->find_effect(object_handle, L"グループ制御");

            if (candidate == nullptr || !ctx->edit->get_effect_enable(candidate)) {
                continue;
            }

            {
                const auto* const alias = ctx->edit->get_object_alias(object_handle);

                if (alias == nullptr) {
                    continue;
                }

                const std::string_view object{alias};

                const auto meta_st = object.find("[Object]");
                const auto empty_st = object.find("[Object.0]");

                if (meta_st == std::string_view::npos || empty_st == std::string_view::npos) {
                    continue;
                }

                const auto empty = object.substr(empty_st, object.find("[Object.1]") - empty_st);

                if (auto st = empty.find("\n対象レイヤー数="); st != std::string_view::npos) {
                    st += sizeof("\n対象レイヤー数=") - 1uz;

                    const auto range = string::ToNumber<int>(empty.substr(st, empty.find_first_of("\r\n", st) - st));

                    if (!range.has_value() || (*range != 0 && *range < layer - i)) {
                        continue;
                    }

                    handles.push_back(candidate);
                    layer = i;

                    const auto meta = object.substr(meta_st, empty_st - meta_st);

                    if (st = meta.find("\ngroup.control="); st != std::string_view::npos) {
                        st += sizeof("\ngroup.control=") - 1uz;

                        if (meta.substr(st, meta.find_first_of("\r\n", st) - st) == "0") {
                            break;
                        }
                    }
                }
            }
        }
    }

    return handles;
}

[[nodiscard]] std::vector<Object::Transform> GetEmpties(int phase, const FILTER_PROC_VIDEO* ctx) {
    if (ctx->object->layer == 0) {
        return {};
    }

    const int frame = ctx->object->frame_s + ctx->object->frame + phase;

    if (frame < 0) {
        return {};
    }

    const auto handles = GetEmptyHandles(frame, ctx);

    std::vector<Object::Transform> empties(handles.size());

    {
        const double point = static_cast<double>(frame);

        for (size_t i = 0; i < handles.size(); ++i) {
            auto* const handle = handles[handles.size() - 1uz - i];

            Object::Transform xform{};

            double v;

            if (ctx->edit->get_effect_track_value(handle, L"X", point, &v)) {
                xform.position.x() = static_cast<float>(v);
            }

            if (ctx->edit->get_effect_track_value(handle, L"Y", point, &v)) {
                xform.position.y() = static_cast<float>(v);
            }

            if (ctx->edit->get_effect_track_value(handle, L"拡大率", point, &v)) {
                if (v < 0.0) {
                    aul::logger::Warning(L"Negative scaling is not supported");
                }

                xform.scale = Eigen::Vector2f::Constant(std::max(static_cast<float>(v) * 0.01f, kEpsilon));
            }

            if (ctx->edit->get_effect_track_value(handle, L"Z軸回転", point, &v)) {
                xform.rotation = ToRadians(static_cast<float>(v));
            }

            empties[i] = std::move(xform);
        }
    }

    return empties;
}

[[nodiscard]] Object::Snapshot GetObjectTransforms(int phase, const FILTER_PROC_VIDEO* ctx) {
    Object::Snapshot snapshot{};

    snapshot.transforms = GetEmpties(phase, ctx);

    snapshot.transforms.emplace_back();
    auto& xform = snapshot.transforms.back();

    {
        const double spf = static_cast<double>(ctx->scene->scale) / ctx->scene->rate;
        OBJECT_IMAGE_PARAM base;

        if (ctx->get_output_image_param(nullptr, phase * spf, &base, sizeof(base))) {
            const auto& offset = store.Get(ctx->object, phase);

            snapshot.pivot = Eigen::Map<const Eigen::Vector2f>(&base.cx) + offset.pivot;
            xform.position = Eigen::Map<const Eigen::Vector2f>(&base.x) + offset.position;
            xform.rotation = ToRadians(base.rz + offset.rotation);

            {
                Eigen::Vector2f scale(base.sx * offset.scale.x(), base.sy * offset.scale.y());

                if ((scale.array() < 0.0f).any()) {
                    aul::logger::Warning(L"Negative scaling is not supported");
                }

                xform.scale = scale.cwiseMax(kEpsilon);
            }
        }
    }

    return snapshot;
}

// 0フレーム以外呼び出し禁止
[[nodiscard]] Object::Snapshot Extrapolate(const Object::Snapshot& zero, const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    static const Object::Transform identity{};

    if (props::extrapolation::value < 1 || props::extrapolation::value > 2) {
        return zero;
    }

    if (ctx->object->index >= 0 && ctx->object->index < ctx->object->num && ctx->object->index < 128) {
        const auto r = std::div(ctx->object->index, 16);

        auto& record = (*props::internal::kRecords[r.quot]->value)[r.rem];

        if (record.num == ctx->object->num) {
            store.Set(ctx->object, record.transforms);
        }

        record.num = store.Get(ctx->object, record.transforms) ? ctx->object->num : 0;
    } else {
        aul::logger::Warning(L"Object index exceeds the cache limit");
    }

    const auto retrodict = [](auto&& value_at) {
        const auto v0 = value_at(0uz);
        const auto v1 = value_at(1uz);
        const auto d0 = v1 - v0;

        if (props::extrapolation::value == 1) {
            return (v0 - d0).eval();
        }

        const auto v2 = value_at(2uz);
        const auto d1 = v2 - v1;

        const auto velocity = ((3.0f * d0) - d1) * 0.5f;
        const auto limit = 3.0f * d0;

        return (v0 - velocity.cwiseMax(limit.cwiseMin(0.0f)).cwiseMin(limit.cwiseMax(0.0f))).eval();
    };

    const size_t samples = static_cast<size_t>(props::extrapolation::value) + 1uz;

    std::array<Object::Snapshot, 3uz> snapshots{zero};

    for (size_t i = 1uz; i < samples; ++i) {
        snapshots[i] = GetObjectTransforms(static_cast<int>(i), ctx);
    }

    size_t depth = 0uz;

    for (size_t i = 0uz; i < samples; ++i) {
        depth = std::max(depth, snapshots[i].transforms.size());
    }

    Object::Snapshot snapshot{};

    snapshot.pivot = retrodict([&](size_t sample) { return snapshots[sample].pivot.array(); });
    snapshot.transforms.resize(depth);

    for (size_t i = 0; i < depth; ++i) {
        std::array<const Object::Transform*, 3uz> inputs{};

        for (size_t sample = 0uz; sample < samples; ++sample) {
            const auto& xforms = snapshots[sample].transforms;
            const size_t offset = depth - xforms.size();
            inputs[sample] = (i < offset) ? &identity : &xforms[i - offset];
        }

        auto& xform = snapshot.transforms[i];

        xform.position = retrodict([&](size_t sample) { return inputs[sample]->position.array(); });

        xform.scale =
            retrodict([&](size_t sample) { return inputs[sample]->scale.array().log(); }).exp().cwiseMax(kEpsilon);

        xform.rotation =
            retrodict([&](size_t sample) { return Eigen::Array<float, 1, 1>::Constant(inputs[sample]->rotation); })[0];
    }

    return snapshot;
}

[[nodiscard]] Object ResolveObject(const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    const float angle = static_cast<float>(props::shutter::angle.value);
    const float amount = std::max(angle / 360.0f, 0.0f);
    const float phase = static_cast<float>(props::shutter::phase.value) / angle;

    Object object;
    auto& state = object.state;

    store.Set(ctx);

    state.start = GetObjectTransforms(0, ctx);

    if (ctx->object->frame == 0) {
        state.end = Extrapolate(state.start, ctx);
    } else {
        state.end = GetObjectTransforms(-1, ctx);
    }

    object.dimensions = Eigen::Vector2i(ctx->object->width, ctx->object->height).cast<float>();

    {
        const Eigen::Vector2f center = object.dimensions * 0.5f;

        state.start.pivot += center;
        state.end.pivot += center;
    }

    if (state.start.transforms.size() == state.end.transforms.size()) {
        state.depth = state.start.transforms.size();
    } else {
        const auto n = std::ssize(state.start.transforms) - std::ssize(state.end.transforms);

        if (n > 0) {
            state.end.transforms.insert(state.end.transforms.begin(), n, Object::Transform{});
        } else {
            state.start.transforms.insert(state.start.transforms.begin(), -n, Object::Transform{});
        }

        state.depth = state.start.transforms.size();
    }

    const auto shift = [phase](Eigen::Vector2f& start, Eigen::Vector2f& end) {
        const Eigen::Vector2f delta = (end - start) * phase;
        start += delta;
        end += delta;
    };

    const auto pivot = Eigen::Translation2f(-state.start.pivot);

    state.end.pivot = lerp(state.start.pivot, state.end.pivot, amount);
    shift(state.start.pivot, state.end.pivot);

    for (size_t i = 0uz; i < state.depth; ++i) {
        auto& start = state.start.transforms[i];
        auto& end = state.end.transforms[i];

        object.transform = object.transform * Eigen::Translation2f(start.position) *
                           Eigen::Rotation2Df(start.rotation) * Eigen::Scaling(start.scale);

        end.position = lerp(start.position, end.position, amount);
        end.scale = lerp(start.scale.cwiseInverse(), end.scale.cwiseInverse(), amount).cwiseInverse();
        end.rotation = std::lerp(start.rotation, end.rotation, amount);

        shift(start.position, end.position);
        shift(start.scale, end.scale);

        start.scale = start.scale.cwiseMax(kEpsilon);
        end.scale = end.scale.cwiseMax(kEpsilon);

        {
            const float delta = (end.rotation - start.rotation) * phase;
            start.rotation += delta;
            end.rotation += delta;
        }

        state.start.world_transform = state.start.world_transform * Eigen::Translation2f(start.position) *
                                      Eigen::Rotation2Df(start.rotation) * Eigen::Scaling(start.scale);

        state.end.world_transform = state.end.world_transform * Eigen::Translation2f(end.position) *
                                    Eigen::Rotation2Df(end.rotation) * Eigen::Scaling(end.scale);
    }

    object.transform = object.transform * pivot;

    return object;
}

[[nodiscard]] int ComputeSamples(const Object& object) {
    const auto& state = object.state;

    const auto start_to_end = state.end.world_transform.inverse() * state.start.world_transform;
    float samples = 0.0f;

    for (int i = 0; i < 4; ++i) {
        const Eigen::Vector2f corner((i & 1) != 0 ? object.dimensions.x() : 0.0f,
                                     (i & 2) != 0 ? object.dimensions.y() : 0.0f);
        const Eigen::Vector2f end = start_to_end * (corner - state.start.pivot) + state.end.pivot;

        samples = std::max(samples, (end - corner).norm());
    }

    return static_cast<int>(samples + 1.0f);
}

[[nodiscard]] Eigen::AlignedBox2f ComputeBox(const Object& object, int samples) {
    const auto& state = object.state;

    const float step = 1.0f / static_cast<float>(samples);
    const auto base_to_world = object.transform.inverse();

    Eigen::AlignedBox2f box(Eigen::Vector2f::Zero(), object.dimensions);

    for (int i = 0; i <= samples; ++i) {
        const auto t = step * static_cast<float>(i);

        Eigen::Affine2f sample_to_base = base_to_world;

        for (size_t j = 0; j < state.depth; ++j) {
            const auto& start = state.start.transforms[j];
            const auto& end = state.end.transforms[j];

            const auto translation = Eigen::Translation2f(lerp(start.position, end.position, t));
            const Eigen::Vector2f scale = lerp(start.scale.cwiseInverse(), end.scale.cwiseInverse(), t).cwiseInverse();
            const auto rotation = Eigen::Rotation2Df(std::lerp(start.rotation, end.rotation, t));

            sample_to_base = sample_to_base * translation * rotation * Eigen::Scaling(scale);
        }

        const Eigen::Vector2f pivot = lerp(object.state.start.pivot, object.state.end.pivot, t);

        const Eigen::Vector2f origin = sample_to_base * -pivot;
        const auto linear = sample_to_base.linear();

        box.extend(origin + linear.cwiseMin(0.0f) * object.dimensions);
        box.extend(origin + linear.cwiseMax(0.0f) * object.dimensions);
    }

    return box;
}

void CreateSubFrameTransforms(const Object& object, int samples, std::vector<Renderer::Affine2D>& xforms) {
    const auto& state = object.state;

    const float step = 1.0f / static_cast<float>(samples - 1);
    xforms.resize(samples);

    for (int i = 0; i < samples; ++i) {
        const float t = step * static_cast<float>(i);
        Eigen::Affine2f subframe_xform = Eigen::Affine2f::Identity();

        for (size_t j = 0uz; j < state.depth; ++j) {
            const auto& start = state.start.transforms[j];
            const auto& end = state.end.transforms[j];

            const auto translation = Eigen::Translation2f(-lerp(start.position, end.position, t));
            const auto scale = lerp(start.scale.cwiseInverse(), end.scale.cwiseInverse(), t);
            const auto rotation = Eigen::Rotation2Df(-std::lerp(start.rotation, end.rotation, t));

            subframe_xform = Eigen::Scaling(scale) * rotation * translation * subframe_xform;
        }

        subframe_xform = Eigen::Translation2f(lerp(state.start.pivot, state.end.pivot, t)) * subframe_xform;

        xforms[i] = {
            .row0 = {subframe_xform(0, 0), subframe_xform(0, 1), subframe_xform(0, 2)},
            .row1 = {subframe_xform(1, 0), subframe_xform(1, 1), subframe_xform(1, 2)},
        };
    }
}

bool Apply(FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    if (ctx->object->width <= 0 || ctx->object->height <= 0) {
        return false;
    }

    if (props::shutter::angle.value < kEpsilon || (ctx->object->frame == 0 && props::extrapolation::value == 0)) {
        return true;
    }

    const auto object = ResolveObject(ctx);

    const int required_samples = ComputeSamples(object);

    if (required_samples < 2) {
        return true;
    }

    const int32_t limit = aul::context::CurrentEditorState() == aul::context::EditorState::kExporting
                              ? static_cast<int32_t>(props::sampling::render::sample_limit.value)
                              : static_cast<int32_t>(props::sampling::viewport::sample_limit.value);

    const int32_t samples = std::min(limit, required_samples);

    if (samples < 2) {
        return true;
    }

    const auto box = ComputeBox(object, std::max(samples / 64, 2));

    {
        Eigen::Vector2f origin;
        Eigen::Vector2f size;

        if (props::should_resize.value) {
            origin = box.min();
            size = box.sizes().array().ceil();
            Eigen::Map<Eigen::Vector2f>(&ctx->param->cx) -= origin + (size - object.dimensions) * 0.5f;
        } else {
            origin = Eigen::Vector2f::Zero();
            size = object.dimensions;
        }

        if (!ctx->copy_image_resource(L"resource:source", nullptr)) {
            aul::logger::Error(L"Failed to copy image resource");
            return false;
        }

        ctx->set_image_data(nullptr, static_cast<int>(size.x()), static_cast<int>(size.y()));

        auto* const dst = ctx->get_image_texture2d();
        auto* const src = ctx->get_image_resource_texture2d(L"resource:source");

        if (src == nullptr || dst == nullptr) {
            aul::logger::Error(L"Failed to get image resource");
            return false;
        }

        thread_local std::vector<Renderer::Affine2D> subframe_xforms;
        CreateSubFrameTransforms(object, samples, subframe_xforms);

        const float mix = std::clamp(static_cast<float>(props::compositing::mix.value) * 0.01f, 0.0f, 1.0f) * 2.0f;
        const float falloff = std::max(static_cast<float>(props::compositing::falloff.value) * 0.01f, 0.0f);
        const float decay = std::pow(std::max(1.0f - falloff, kEpsilon), 1.0f / static_cast<float>(samples - 1));

        const Renderer::Param param{
            .transform =
                {
                    .row0 =
                        {
                            object.transform(0, 0),
                            object.transform(0, 1),
                            object.transform(0, 2),
                        },
                    .row1 =
                        {
                            object.transform(1, 0),
                            object.transform(1, 1),
                            object.transform(1, 2),
                        },
                },
            .origin =
                {
                    origin.x(),
                    origin.y(),
                },
            .texel =
                {
                    1.0f / object.dimensions.x(),
                    1.0f / object.dimensions.y(),
                },
            .mix =
                {
                    std::min(2.0f - mix, 1.0f),
                    std::min(mix, 1.0f),
                },
            .decay = decay,
            .samples = samples,
        };

        const auto ec = renderer.Render(dst, [src, &param](Renderer::Context& ctx) -> std::error_code {
            return ctx.Draw(src, subframe_xforms, param);
        });

        if (ec != std::error_code{}) {
            aul::logger::Error(string::ToWString(string::AsUTF8(ec.message())));
            return false;
        }
    }

    if (props::should_print_diagnostics.value) {
        aul::logger::Log(
            std::format(L"\n"
                        L"Effect ID       : {}\n"
                        L"Index           : {}\n"
                        L"Required Samples: {}\n"
                        L"Samples         : {}\n",
                        ctx->object->effect_id, ctx->object->index, required_samples, samples));
    }

    return true;
}

constinit void* props[] = {
    &properties::shutter::name,
    &properties::shutter::angle,
    &properties::shutter::phase,
    &properties::sampling::name,
    &properties::sampling::viewport::name,
    &properties::sampling::viewport::sample_limit,
    &properties::sampling::render::name,
    &properties::sampling::render::sample_limit,
    &properties::compositing::name,
    &properties::compositing::mix,
    &properties::compositing::falloff,
    &properties::additional_options,
    &properties::extrapolation::control,
    &properties::should_resize,
    &properties::should_print_diagnostics,
    &properties::internal::records0,
    &properties::internal::records1,
    &properties::internal::records2,
    &properties::internal::records3,
    &properties::internal::records4,
    &properties::internal::records5,
    &properties::internal::records6,
    &properties::internal::records7,
    nullptr,
};

constinit FILTER_PLUGIN_TABLE desc{
    .flag = FILTER_PLUGIN_TABLE::FLAG_VIDEO,
    .name = L"ObjectMotionBlur_LK",
    .label = L"ぼかし",
    .information = L"ObjectMotionBlur_LK v" VERSION L" by Korarei",
    .items = props,
    .func_proc_video = Apply,
    .func_proc_audio = nullptr,
    .func_create = nullptr,
    .func_destroy = nullptr,
};
}  // namespace

namespace blur::object {
void Register(HOST_APP_TABLE* host) {
    host->register_filter_plugin(&desc);

    host->register_clear_cache_handler([]([[maybe_unused]] EDIT_SECTION* edit) {
        renderer.Reset();
        store.Reset();
    });
}

void Unregister() {
    renderer.Reset();
    store.Reset();
}
}  // namespace blur::object
