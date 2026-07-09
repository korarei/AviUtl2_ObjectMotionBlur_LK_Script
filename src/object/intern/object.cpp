#include "../object.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>  // IWYU pragma: keep
#include <format>
#include <numbers>
#include <vector>

#include <Eigen/Dense>
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
        Snapshot previous{};
        Snapshot current{};
        size_t depth = 0u;
    };

    Eigen::Vector2f dimensions = Eigen::Vector2f::Zero();
    State state{};
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

[[nodiscard]] inline Eigen::Vector2f lerp(const Eigen::Vector2f& a, const Eigen::Vector2f& b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] std::vector<EFFECT_HANDLE> GetEmptyHandles(int frame, const FILTER_PROC_VIDEO* ctx) {
    std::vector<EFFECT_HANDLE> handles{};
    handles.reserve(ctx->object->layer);

    {
        int layer = ctx->object->layer - 1;

        for (int i = layer; i >= 0; --i) {
            /*
            v2.0.54 では非対応
            if (!ctx->edit->get_layer_enable(i)) {
                continue;
            }
            */

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

                {
                    const auto meta = object.substr(meta_st, empty_st - meta_st);

                    if (auto st = meta.find("\ngroup.control="); st != std::string_view::npos) {
                        st += sizeof("\ngroup.control=") - 1uz;

                        if (meta.substr(st, meta.find_first_of("\r\n", st) - st) == "0") {
                            break;
                        }
                    }
                }

                const auto empty = object.substr(empty_st, object.find("[Object.1]") - empty_st);

                if (auto st = empty.find("\n対象レイヤー数="); st != std::string_view::npos) {
                    st += sizeof("\n対象レイヤー数=") - 1uz;

                    const auto range = string::ToNumber<int>(empty.substr(st, empty.find_first_of("\r\n", st) - st));

                    if (!range.has_value() || (*range != 0 && *range < layer - i)) {
                        continue;
                    }
                } else {
                    continue;
                }
            }

            handles.push_back(candidate);
            layer = i;
        }
    }

    return handles;
}

[[nodiscard]] std::vector<Object::Transform> GetEmpties(int offset, const FILTER_PROC_VIDEO* ctx) {
    if (ctx->object->layer == 0) {
        return {};
    }

    const int frame = ctx->object->frame_s + ctx->object->frame + offset;

    if (frame < 0) {
        return {};
    }

    const auto handles = GetEmptyHandles(frame, ctx);

    std::vector<Object::Transform> empties(handles.size());

    {
        const double point = static_cast<double>(frame);

        for (size_t i = 0; i < handles.size(); ++i) {
            auto* const handle = handles[handles.size() - 1uz - i];

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
                }

                transform.scale = Eigen::Vector2f::Constant(std::max(static_cast<float>(v) * 0.01f, kEpsilon));
            }

            if (ctx->edit->get_effect_track_value(handle, L"Z軸回転", point, &v)) {
                transform.rotation = ToRadians(static_cast<float>(v));
            }

            empties[i] = std::move(transform);
        }
    }

    return empties;
}

[[nodiscard]] Object::Snapshot GetObjectTransforms(int offset, const FILTER_PROC_VIDEO* ctx) {
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
                }

                transform.scale = scale.cwiseMax(kEpsilon);
            }
        }
    }

    return snapshot;
}

[[nodiscard]] float ExtrapolateScalar(const std::vector<float>& values, int degree) {
    const int rows = static_cast<int>(values.size());
    const int cols = degree + 1;

    Eigen::MatrixXf a(rows, cols);
    Eigen::VectorXf b(rows);

    for (int row = 0; row < rows; ++row) {
        const float t = static_cast<float>(row);
        const float w = std::sqrt(std::exp(-t));
        float x = 1.0f;

        for (int col = 0; col < cols; ++col) {
            a(row, col) = x * w;
            x *= t;
        }

        b(row) = values[static_cast<size_t>(row)] * w;
    }

    const Eigen::VectorXf fs = a.colPivHouseholderQr().solve(b);

    float result = 0.0f;
    float x = 1.0f;

    for (int col = 0; col < cols; ++col) {
        result += fs(col) * x;
        x *= -1.0f;
    }

    return result;
}

[[nodiscard]] Eigen::Vector2f ExtrapolateVector(const std::vector<Eigen::Vector2f>& values, int degree) {
    std::vector<float> xs(values.size());
    std::vector<float> ys(values.size());

    for (size_t i = 0; i < values.size(); ++i) {
        xs[i] = values[i].x();
        ys[i] = values[i].y();
    }

    return {ExtrapolateScalar(xs, degree), ExtrapolateScalar(ys, degree)};
}

// 0フレーム以外呼び出し禁止
[[nodiscard]] Object::Snapshot Extrapolate(const Object::Snapshot& zero, const FILTER_PROC_VIDEO* ctx) {
    const int degree = properties::extrapolation::value;

    if (degree < 1 || degree > 2) {
        return zero;
    }

    const size_t samples = static_cast<size_t>(degree) + 3uz;
    std::vector<Object::Snapshot> snapshots;
    snapshots.reserve(samples);
    snapshots.push_back(zero);

    for (size_t i = 1; i < samples; ++i) {
        snapshots.push_back(GetObjectTransforms(static_cast<int>(i), ctx));
    }

    size_t depth = 0uz;

    for (const auto& snapshot : snapshots) {
        depth = std::max(depth, snapshot.transforms.size());
    }

    std::vector<Eigen::Vector2f> pivots(samples);

    for (size_t i = 0; i < samples; ++i) {
        pivots[i] = snapshots[i].pivot;
    }

    std::vector<Object::Transform> transforms(depth);

    for (size_t i = 0; i < depth; ++i) {
        std::vector<Eigen::Vector2f> positions(samples);
        std::vector<Eigen::Vector2f> scales(samples);
        std::vector<float> rotations(samples);

        for (size_t j = 0; j < samples; ++j) {
            const size_t offset = depth - snapshots[j].transforms.size();
            const Object::Transform identity{};
            const auto& transform = (i < offset) ? identity : snapshots[j].transforms[i - offset];

            positions[j] = transform.position;
            scales[j] = transform.scale.cwiseMax(kEpsilon).array().log();
            rotations[j] = transform.rotation;
        }

        transforms[i] = {
            .position = ExtrapolateVector(positions, degree),
            .scale = ExtrapolateVector(scales, degree).array().exp().cwiseMax(kEpsilon),
            .rotation = ExtrapolateScalar(rotations, degree),
        };
    }

    return Object::Snapshot{
        .pivot = ExtrapolateVector(pivots, degree),
        .transforms = std::move(transforms),
    };
}

[[nodiscard]] Object ResolveObject(float amount, const FILTER_PROC_VIDEO* ctx) {
    Object object;
    auto& state = object.state;

    store.Set(ctx);

    state.current = GetObjectTransforms(0, ctx);

    if (ctx->object->frame == 0) {
        state.previous = Extrapolate(state.current, ctx);
    } else {
        state.previous = GetObjectTransforms(-1, ctx);
    }

    object.dimensions = Eigen::Vector2i(ctx->object->width, ctx->object->height).cast<float>();

    {
        const Eigen::Vector2f center = object.dimensions * 0.5f;

        state.current.pivot += center;
        state.previous.pivot += center;
    }

    if (state.current.transforms.size() == state.previous.transforms.size()) {
        state.depth = state.current.transforms.size();
    } else {
        const auto n = std::ssize(state.current.transforms) - std::ssize(state.previous.transforms);

        if (n > 0ll) {
            state.previous.transforms.insert(state.previous.transforms.begin(), n, Object::Transform{});
        } else {
            state.current.transforms.insert(state.current.transforms.begin(), -n, Object::Transform{});
        }

        state.depth = state.current.transforms.size();
    }

    state.previous.pivot = lerp(state.current.pivot, state.previous.pivot, amount);

    for (size_t i = 0uz; i < state.depth; ++i) {
        const auto& curr = state.current.transforms[i];
        auto& prev = state.previous.transforms[i];

        prev.position = lerp(curr.position, prev.position, amount);
        prev.scale = lerp(curr.scale.cwiseInverse(), prev.scale.cwiseInverse(), amount).cwiseInverse();
        prev.rotation = std::lerp(curr.rotation, prev.rotation, amount);

        state.current.world_transform = state.current.world_transform * Eigen::Translation2f(curr.position) *
                                        Eigen::Rotation2Df(curr.rotation) * Eigen::Scaling(curr.scale);

        state.previous.world_transform = state.previous.world_transform * Eigen::Translation2f(prev.position) *
                                         Eigen::Rotation2Df(prev.rotation) * Eigen::Scaling(prev.scale);
    }

    return object;
}

[[nodiscard]] int ComputeSamples(const Object& object) {
    const auto& state = object.state;

    const auto curr_to_prev = state.previous.world_transform.inverse() * state.current.world_transform;
    float max = 0.0f;

    for (int i = 0; i < 4; ++i) {
        const Eigen::Vector2f corner((i & 1) != 0 ? object.dimensions.x() : 0.0f,
                                     (i & 2) != 0 ? object.dimensions.y() : 0.0f);
        const Eigen::Vector2f prev = curr_to_prev * (corner - state.current.pivot) + state.previous.pivot;

        max = std::max(max, (prev - corner).norm());
    }

    return static_cast<int>(max + 1.0f);
}

[[nodiscard]] Eigen::AlignedBox2f ComputeBox(const Object& object, int samples) {
    const auto& state = object.state;

    const float step = 1.0f / static_cast<float>(samples);
    const auto curr_to_world = state.current.world_transform.inverse();

    Eigen::AlignedBox2f box(Eigen::Vector2f::Zero(), object.dimensions);

    for (int i = 0; i < samples; ++i) {
        const auto t = 1.0f - (step * static_cast<float>(i));

        Eigen::Affine2f curr_to_prev = curr_to_world;

        for (size_t j = 0; j < state.depth; ++j) {
            const auto& curr = state.current.transforms[j];
            const auto& prev = state.previous.transforms[j];

            const auto translation = Eigen::Translation2f(lerp(curr.position, prev.position, t));
            const Eigen::Vector2f scale = lerp(curr.scale.cwiseInverse(), prev.scale.cwiseInverse(), t).cwiseInverse();
            const auto rotation = Eigen::Rotation2Df(std::lerp(curr.rotation, prev.rotation, t));

            curr_to_prev = curr_to_prev * translation * rotation * Eigen::Scaling(scale);
        }

        const Eigen::Vector2f pivot = lerp(object.state.current.pivot, object.state.previous.pivot, t);

        const Eigen::Vector2f origin = curr_to_prev * -pivot + object.state.current.pivot;
        const auto linear = curr_to_prev.linear();

        box.extend(origin + linear.cwiseMin(0.0f) * object.dimensions);
        box.extend(origin + linear.cwiseMax(0.0f) * object.dimensions);
    }

    return box;
}

[[nodiscard]] std::vector<d3d::Renderer::SampleTransform> CreateSampleTransforms(const Object& object, int samples) {
    samples -= 1;

    const auto& state = object.state;

    const float step = 1.0f / static_cast<float>(samples);
    std::vector<d3d::Renderer::SampleTransform> xforms(samples);

    for (int i = 1; i <= samples; ++i) {
        const float t = step * static_cast<float>(i);
        Eigen::Affine2f prev_to_world = Eigen::Affine2f::Identity();

        for (size_t j = 0uz; j < state.depth; ++j) {
            const auto& curr = state.current.transforms[j];
            const auto& prev = state.previous.transforms[j];

            const auto translation = Eigen::Translation2f(-lerp(curr.position, prev.position, t));
            const auto scale = lerp(curr.scale.cwiseInverse(), prev.scale.cwiseInverse(), t);
            const auto rotation = Eigen::Rotation2Df(-std::lerp(curr.rotation, prev.rotation, t));

            prev_to_world = Eigen::Scaling(scale) * rotation * translation * prev_to_world;
        }

        prev_to_world = Eigen::Translation2f(lerp(state.current.pivot, state.previous.pivot, t)) * prev_to_world;

        xforms[i - 1] = {
            .row0 = {prev_to_world(0, 0), prev_to_world(0, 1), prev_to_world(0, 2)},
            .row1 = {prev_to_world(1, 0), prev_to_world(1, 1), prev_to_world(1, 2)},
        };
    }

    return xforms;
}

bool Apply(FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    if (ctx->object->width <= 0 || ctx->object->height <= 0) {
        return false;
    }

    const auto amount = std::clamp(static_cast<float>(props::shutter_angle.value) / 360.0f, 0.0f, 1.0f);

    if (amount < kEpsilon || (ctx->object->frame == 0 && props::extrapolation::value == 0)) {
        return true;
    }

    const auto object = ResolveObject(amount, ctx);

    const int required_samples = ComputeSamples(object);

    if (required_samples < 2) {
        return true;
    }

    const int limit = aul::Context::CurrentSessionState() == aul::Context::SessionState::kRendering
                          ? static_cast<int>(props::sampling::render::sample_limit.value)
                          : static_cast<int>(props::sampling::viewport::sample_limit.value);

    const int samples = std::min(limit, required_samples);

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

        const auto xforms = CreateSampleTransforms(object, samples);

        const float mix = std::clamp(static_cast<float>(properties::compositing::mix.value) * 0.02f, 0.0f, 2.0f);

        const d3d::Renderer::Param param{
            .base_transform_row0 =
                {
                    object.state.current.world_transform(0, 0),
                    object.state.current.world_transform(0, 1),
                    object.state.current.world_transform(0, 2),
                },
            .base_transform_row1 =
                {
                    object.state.current.world_transform(1, 0),
                    object.state.current.world_transform(1, 1),
                    object.state.current.world_transform(1, 2),
                },
            .pivot =
                {
                    object.state.current.pivot.x(),
                    object.state.current.pivot.y(),
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
