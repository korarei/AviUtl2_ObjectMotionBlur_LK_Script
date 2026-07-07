#include "../object.hpp"

#include <algorithm>
#include <cstdint>  // IWYU pragma: keep
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
namespace d3d = blur::object::direct3d;

using Offset = cache::Store::Transform;

constexpr float kEpsilon = Eigen::NumTraits<float>::dummy_precision();

struct Object {
    struct Transform {
        Eigen::Vector2f position = Eigen::Vector2f::Zero();
        Eigen::Vector2f scale = Eigen::Vector2f::Ones();
        float rotation = 0.0f;
    };

    struct Snapshot {
        Eigen::Vector2f pivot = Eigen::Vector2f::Zero();
        std::vector<Transform> transforms;
    };

    Eigen::Affine2f transform = Eigen::Affine2f::Identity();
    Eigen::Vector2f size = Eigen::Vector2f::Zero();

    struct {
        Snapshot previous{};
        Snapshot current{};
        size_t size = 0u;
    } state{};
};

struct Box {
    Eigen::AlignedBox2f min;
    Eigen::AlignedBox2f max;
};

namespace properties {
FILTER_ITEM_TRACK shutter_angle(L"Shutter Angle", 180.0, 0.0, 360.0, 0.01);
namespace extrapolation {
FILTER_ITEM_SELECT::ITEM contents[] = {{L"None", 0}, {L"Linear", 1}, {L"Quadratic", 2}, {nullptr, -1}};
FILTER_ITEM_SELECT control(L"Extrapolation", 2, contents);
auto& value = control.value;
}  // namespace extrapolation
FILTER_ITEM_CHECK should_resize(L"Resize", true);
namespace sampling {
FILTER_ITEM_GROUP name(L"Sampling", false);
namespace viewport {
FILTER_ITEM_SEPARATOR name(L"Viewport");
FILTER_ITEM_TRACK sample_limit(L"Sampling::Viewport::Sample Limit", 128.0, 1.0, 4096.0, 1.0);
}  // namespace viewport
namespace render {
FILTER_ITEM_SEPARATOR name(L"Render");
FILTER_ITEM_TRACK sample_limit(L"Sampling::Render::Sample Limit", 512.0, 1.0, 4096.0, 1.0);
}  // namespace render
}  // namespace sampling
namespace compositing {
FILTER_ITEM_GROUP name(L"Compositing", false);
FILTER_ITEM_TRACK mix(L"Compositing::Mix", 100.0, 0.0, 100.0, 0.01);
}  // namespace compositing
FILTER_ITEM_GROUP additional_options(L"Additional Options", false);
FILTER_ITEM_CHECK should_print_diagnostics(L"Diagnostics", false);
}  // namespace properties

d3d::Renderer renderer{};
cache::Store store{};

[[nodiscard]] constexpr float ToRadians(float deg) noexcept {
    constexpr float f = std::numbers::pi_v<float> / 180.0f;
    return deg * f;
}

[[nodiscard]] std::vector<Object::Transform> GetEmpties(int offset, const FILTER_PROC_VIDEO* ctx) {
    if (ctx->object->layer == 0) {
        return {};
    }

    const int frame = ctx->object->frame_s + ctx->object->frame + offset;

    if (frame < 0) {
        return {};
    }

    std::vector<EFFECT_HANDLE> candidates{};
    candidates.reserve(ctx->object->layer);

    {
        int layer = ctx->object->layer - 1;

        for (int i = ctx->object->layer - 1; i >= 0; --i) {
            auto* const handle = ctx->edit->find_object(i, frame);

            if (handle == nullptr || ctx->edit->get_object_layer_frame(handle).start > frame) {
                continue;
            }

            auto* const candidate = ctx->edit->find_effect(handle, L"グループ制御");

            if (candidate == nullptr || !ctx->edit->get_effect_enable(candidate)) {
                continue;
            }

            const auto* const range_str = ctx->edit->get_effect_item_value(candidate, L"対象レイヤー数");

            int range;
            if (range_str == nullptr || !string::ToNumber(range_str, range) || (range != 0 && range < layer - i)) {
                continue;
            }

            candidates.push_back(candidate);
            layer = i;
        }
    }

    std::vector<Object::Transform> empties(candidates.size());

    {
        const double point = static_cast<double>(frame);

        for (size_t i = 0; i < candidates.size(); ++i) {
            auto* const handle = candidates[candidates.size() - 1uz - i];

            Object::Transform transform{};

            double v;

            if (ctx->edit->get_effect_track_value(handle, L"X", point, &v)) {
                transform.position.x() = static_cast<float>(v);
            }

            if (ctx->edit->get_effect_track_value(handle, L"Y", point, &v)) {
                transform.position.y() = static_cast<float>(v);
            }

            if (ctx->edit->get_effect_track_value(handle, L"拡大率", point, &v)) {
                if (v < 0.0) {
                    aul::Logger::Warning(L"Negative scaling is not supported");
                    transform.scale = Eigen::Vector2f::Constant(0.0f);
                } else {
                    transform.scale = Eigen::Vector2f::Constant(static_cast<float>(v) * 0.01f);
                }
            }

            if (ctx->edit->get_effect_track_value(handle, L"Z軸回転", point, &v)) {
                transform.rotation = ToRadians(static_cast<float>(v));
            }

            empties[i] = std::move(transform);
        }
    }

    return empties;
}

[[nodiscard]] Object::Snapshot GetObjectSnapshot(int offset, const FILTER_PROC_VIDEO* ctx) {
    Object::Snapshot snapshot{};

    snapshot.transforms = GetEmpties(offset, ctx);

    snapshot.transforms.emplace_back();
    auto& transform = snapshot.transforms.back();

    {
        const double spf = static_cast<double>(ctx->scene->scale) / ctx->scene->rate;
        OBJECT_IMAGE_PARAM base;

        if (ctx->get_output_image_param(nullptr, offset * spf, &base, sizeof(base))) {
            const auto& xform = store.Get(ctx->object, offset);

            snapshot.pivot = Eigen::Map<const Eigen::Vector2f>(&base.cx) + xform.pivot;
            transform.position = Eigen::Map<const Eigen::Vector2f>(&base.x) + xform.position;
            transform.rotation = ToRadians(base.rz + xform.rotation);

            {
                Eigen::Vector2f scale(base.sx * xform.scale.x(), base.sy * xform.scale.y());

                if ((scale.array() < 0.0f).any()) {
                    aul::Logger::Warning(L"Negative scaling is not supported");
                    transform.scale = scale.cwiseMax(0.0f);
                } else {
                    transform.scale = std::move(scale);
                }
            }
        }
    }

    return snapshot;
}

// 0フレーム以外呼び出し禁止
[[nodiscard]] std::optional<Object::Snapshot> Extrapolate(const Object::Snapshot& zero, const FILTER_PROC_VIDEO* ctx) {
    switch (properties::extrapolation::value) {
        case 1: {
            const auto one = GetObjectSnapshot(1, ctx);

            const auto extrapolate = [](const Object::Transform& t0, const Object::Transform& t1) {
                return Object::Transform{
                    .position = t0.position * 2.0f - t1.position,
                    .scale = (t0.scale * 2.0f - t1.scale).cwiseMax(0.0f),
                    .rotation = (t0.rotation * 2.0f) - t1.rotation,
                };
            };

            std::vector<Object::Transform> prev(std::max(zero.transforms.size(), one.transforms.size()));

            auto z = zero.transforms.rbegin();
            auto o = one.transforms.rbegin();

            for (auto it = prev.rbegin(); it != prev.rend(); ++it) {
                const auto& t0 = (z != zero.transforms.rend()) ? *z++ : Object::Transform{};
                const auto& t1 = (o != one.transforms.rend()) ? *o++ : Object::Transform{};
                *it = extrapolate(t0, t1);
            }

            return Object::Snapshot{
                .pivot = zero.pivot * 2.0f - one.pivot,
                .transforms = std::move(prev),
            };
        }
        case 2: {
            const auto one = GetObjectSnapshot(1, ctx);
            const auto two = GetObjectSnapshot(2, ctx);

            const auto extrapolate = [](const Object::Transform& t0, const Object::Transform& t1,
                                        const Object::Transform& t2) {
                return Object::Transform{
                    .position = t0.position * 3.0f - t1.position * 3.0f + t2.position,
                    .scale = (t0.scale * 3.0f - t1.scale * 3.0f + t2.scale).cwiseMax(0.0f),
                    .rotation = (t0.rotation * 3.0f) - (t1.rotation * 3.0f) + t2.rotation,
                };
            };

            std::vector<Object::Transform> prev(
                std::max({zero.transforms.size(), one.transforms.size(), two.transforms.size()}));

            auto z = zero.transforms.rbegin();
            auto o = one.transforms.rbegin();
            auto t = two.transforms.rbegin();

            for (auto it = prev.rbegin(); it != prev.rend(); ++it) {
                const auto& t0 = (z != zero.transforms.rend()) ? *z++ : Object::Transform{};
                const auto& t1 = (o != one.transforms.rend()) ? *o++ : Object::Transform{};
                const auto& t2 = (t != two.transforms.rend()) ? *t++ : Object::Transform{};
                *it = extrapolate(t0, t1, t2);
            }

            return Object::Snapshot{
                .pivot = zero.pivot * 3.0f - one.pivot * 3.0f + two.pivot,
                .transforms = std::move(prev),
            };
        }
        default:
            return std::nullopt;
    }
}

[[nodiscard]] std::optional<Object> CreateObject(const FILTER_PROC_VIDEO* ctx) {
    Object object;

    store.Set(ctx);

    object.state.current = GetObjectSnapshot(0, ctx);

    if (ctx->object->frame == 0) {
        if (const auto prev = Extrapolate(object.state.current, ctx); prev.has_value()) {
            object.state.previous = *prev;
        } else {
            return std::nullopt;
        }
    } else {
        object.state.previous = GetObjectSnapshot(-1, ctx);
    }

    for (const auto& xform : object.state.current.transforms) {
        object.transform = object.transform * Eigen::Translation2f(xform.position) *
                           Eigen::Rotation2Df(xform.rotation) * Eigen::Scaling(xform.scale);
    }

    if (object.transform.linear().determinant() < kEpsilon) {
        aul::Logger::Warning(L"Singular transformation (determinant is zero)");
        return std::nullopt;
    }

    object.size = Eigen::Vector2f(static_cast<float>(ctx->object->width), static_cast<float>(ctx->object->height));

    {
        const Eigen::Vector2f center = object.size * 0.5f;

        object.state.current.pivot += center;
        object.state.previous.pivot += center;
    }

    if (object.state.current.transforms.size() == object.state.previous.transforms.size()) {
        object.state.size = object.state.current.transforms.size();
        return object;
    }

    {
        const auto n = std::ssize(object.state.current.transforms) - std::ssize(object.state.previous.transforms);

        if (n > 0ll) {
            object.state.previous.transforms.insert(object.state.previous.transforms.begin(), n, Object::Transform{});
        } else {
            object.state.current.transforms.insert(object.state.current.transforms.begin(), -n, Object::Transform{});
        }

        object.state.size = object.state.current.transforms.size();
    }

    return object;
}

[[nodiscard]] std::optional<Box> ComputeBox(const Object& object, float amount, int samples = 3) {
    constexpr float inf = Eigen::NumTraits<float>::infinity();

    auto lerp = [](const Eigen::Vector2f& a, const Eigen::Vector2f& b, float t) { return a + (b - a) * t; };

    Box box{
        .min = Eigen::AlignedBox2f(Eigen::Vector2f::Constant(-inf), Eigen::Vector2f::Constant(inf)),
        .max = Eigen::AlignedBox2f(Eigen::Vector2f::Zero(), object.size),
    };

    const float step = amount / static_cast<float>(samples);
    const auto curr_to_world = object.transform.inverse();

    for (int i = 0; i < samples; ++i) {
        const auto t = amount - (step * static_cast<float>(i));

        Eigen::Affine2f curr_to_prev = curr_to_world;

        for (size_t j = 0; j < object.state.size; ++j) {
            const auto& curr = object.state.current.transforms[j];
            const auto& prev = object.state.previous.transforms[j];

            if ((curr.scale.array() < kEpsilon).any() || (prev.scale.array() < kEpsilon).any()) {
                aul::Logger::Warning(L"Singular transformation (determinant is zero)");
                return std::nullopt;
            }

            curr_to_prev = curr_to_prev * Eigen::Translation2f(lerp(curr.position, prev.position, t)) *
                           Eigen::Rotation2Df(std::lerp(curr.rotation, prev.rotation, t)) *
                           Eigen::Scaling(lerp(curr.scale.cwiseInverse(), prev.scale.cwiseInverse(), t).cwiseInverse());
        }

        const Eigen::Vector2f pivot = lerp(object.state.current.pivot, object.state.previous.pivot, t);

        const Eigen::Vector2f origin = curr_to_prev * -pivot + object.state.current.pivot;
        const auto linear = curr_to_prev.linear();
        const Eigen::Vector2f pos = origin + linear.cwiseMin(0.0f) * object.size;
        const Eigen::Vector2f end = origin + linear.cwiseMax(0.0f) * object.size;

        box.min.min() = box.min.min().cwiseMax(pos);
        box.min.max() = box.min.max().cwiseMin(end);

        box.max.extend(pos);
        box.max.extend(end);
    }

    // box.min が inf ではないはず

    return box;
}

[[nodiscard]] std::vector<d3d::Renderer::SampleAffine> CreateSampleAffines(const Object& object, float amount,
                                                                           int samples) {
    auto lerp = [](const Eigen::Vector2f& a, const Eigen::Vector2f& b, float t) { return a + (b - a) * t; };

    const float step = amount / static_cast<float>(samples - 1);
    std::vector<d3d::Renderer::SampleAffine> xforms(samples - 1);

    for (int i = 1; i < samples; ++i) {
        const float t = step * static_cast<float>(i);
        Eigen::Affine2f transform = Eigen::Affine2f::Identity();

        for (size_t j = 0uz; j < object.state.size; ++j) {
            const auto& curr = object.state.current.transforms[j];
            const auto& prev = object.state.previous.transforms[j];

            const Eigen::Vector2f position = lerp(curr.position, prev.position, t);
            const Eigen::Vector2f scale = lerp(curr.scale.cwiseInverse(), prev.scale.cwiseInverse(), t);
            const float angle = std::lerp(curr.rotation, prev.rotation, t);
            const Eigen::Matrix2f linear = scale.asDiagonal() * Eigen::Rotation2Df(-angle).toRotationMatrix();

            Eigen::Affine2f xform = Eigen::Affine2f::Identity();
            xform.linear() = linear;
            xform.translation() = linear * -position;

            transform = xform * transform;
        }

        transform = Eigen::Translation2f(lerp(object.state.current.pivot, object.state.previous.pivot, t)) * transform;

        auto& xform = xforms[static_cast<size_t>(i - 1)];
        xform.row0[0] = transform(0, 0);
        xform.row0[1] = transform(0, 1);
        xform.row0[2] = transform(0, 2);
        xform.row1[0] = transform(1, 0);
        xform.row1[1] = transform(1, 1);
        xform.row1[2] = transform(1, 2);
    }

    return xforms;
}

bool Apply(FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    if (ctx->object->width <= 0 || ctx->object->height <= 0) {
        return false;
    }

    const auto amount = std::clamp(static_cast<float>(props::shutter_angle.value) / 360.0f, 0.0f, 1.0f);

    if (amount < kEpsilon) {
        return true;
    }

    const auto object = CreateObject(ctx);

    if (!object.has_value()) {
        return true;
    }

    const auto box = ComputeBox(*object, amount);

    if (!box.has_value()) {
        return true;
    }

    Eigen::Vector2f size = box->max.sizes();

    const int required_samples = static_cast<int>((size - box->min.sizes().cwiseMax(0.0f)).norm() + 1.0f);

    if (required_samples < 2) {
        return true;
    }

    const int limit = aul::Context::handle()->get_edit_state() == EDIT_HANDLE::EDIT_STATE_SAVE
                          ? static_cast<int>(props::sampling::render::sample_limit.value)
                          : static_cast<int>(props::sampling::viewport::sample_limit.value);

    const int samples = std::min(limit, required_samples);

    if (samples < 2) {
        return true;
    }

    {
        Eigen::Vector2f origin = Eigen::Vector2f::Zero();

        if (props::should_resize.value) {
            origin = box->max.min();
            Eigen::Map<Eigen::Vector2f>(&ctx->param->cx) -= box->max.min() + (size - object->size) * 0.5f;
        } else {
            size = object->size;
        }

        if (!ctx->copy_image_resource(L"resource:src", nullptr)) {
            aul::Logger::Error(L"Failed to copy image resource");
            return false;
        }

        ctx->set_image_data(nullptr, static_cast<int>(size.x()), static_cast<int>(size.y()));

        if (!ctx->clear_image_resource(nullptr, {0, 0, 0, 0})) {
            aul::Logger::Error(L"Failed to clear image resource");
            return false;
        }

        auto* const src = ctx->get_image_resource_texture2d(L"resource:src");
        auto* const dst = ctx->get_image_texture2d();

        if (src == nullptr || dst == nullptr) {
            aul::Logger::Error(L"Failed to get image resource");
            return false;
        }

        const auto xforms = CreateSampleAffines(*object, amount, samples);

        const float mix = std::clamp(static_cast<float>(properties::compositing::mix.value) * 0.02f, 0.0f, 2.0f);

        const d3d::Renderer::Param param{
            .row0 =
                {
                    object->transform(0, 0),
                    object->transform(0, 1),
                    object->transform(0, 2),
                    0.0f,
                },
            .row1 =
                {
                    object->transform(1, 0),
                    object->transform(1, 1),
                    object->transform(1, 2),
                    0.0f,
                },
            .pivot =
                {
                    object->state.current.pivot.x(),
                    object->state.current.pivot.y(),
                },
            .origin =
                {
                    origin.x(),
                    origin.y(),
                },
            .texel =
                {
                    1.0f / object->size.x(),
                    1.0f / object->size.y(),
                },
            .amount = amount,
            .samples = samples,
            .mix =
                {
                    std::min(2.0f - mix, 1.0f),
                    std::min(mix, 1.0f),
                },
        };

        const auto result = renderer.Render(
            dst, [&xforms, &param, src](d3d::Renderer::Context& ctx) { return ctx.Draw(src, xforms, param); });

        if (!result.has_value()) {
            aul::Logger::Error(result.error());
            return false;
        }
    }

    if (properties::should_print_diagnostics.value) {
        aul::Logger::Log(
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
    &properties::shutter_angle,
    &properties::extrapolation::control,
    &properties::should_resize,
    &properties::sampling::name,
    &properties::sampling::viewport::name,
    &properties::sampling::viewport::sample_limit,
    &properties::sampling::render::name,
    &properties::sampling::render::sample_limit,
    &properties::compositing::name,
    &properties::compositing::mix,
    &properties::additional_options,
    &properties::should_print_diagnostics,
    nullptr,
};

constinit FILTER_PLUGIN_TABLE info{
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
void Init(HOST_APP_TABLE* host) {
    host->register_filter_plugin(&info);

    host->register_clear_cache_handler([]([[maybe_unused]] EDIT_SECTION* edit) { store.Reset(); });
}

void Deinit() {
    renderer.Reset();
    store.Reset();
}
}  // namespace blur::object
