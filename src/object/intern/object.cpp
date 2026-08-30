#include "../object.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <vector>

#include <Eigen/Geometry>

#pragma warning(push)
#pragma warning(disable : 4201)  // 非標準の無名構造体 (filter2.h FILTER_ITEM_COLOR)
#include <filter2.h>
#pragma warning(pop)

#include <intern/aviutl/aviutl.hpp>
#include <intern/string.hpp>

#include "instance.hpp"
#include "render.hpp"
#include "transform.hpp"

namespace {
namespace aul = blur::aviutl;
namespace string = blur::string;
namespace renderer = blur::object::renderer;

using FrameMapping = blur::object::FrameMapping;
using Instance = blur::object::Instance;
using Sample = blur::object::Sample;
using State = blur::object::State;
using Transform = blur::object::Transform;

constexpr float kEpsilon = Eigen::NumTraits<float>::dummy_precision();

struct Object {
    struct Snapshot {
        Eigen::Vector2f pivot = Eigen::Vector2f::Zero();
        std::vector<Transform> transforms;
    };

    template <typename T>
    struct Range {
        T origin;
        T extent;
    };

    struct Rotation {
        float cos;
        float sin;
    };

    struct Rig {
        struct Link {
            Range<Eigen::Vector2f> position;
            Range<Eigen::Vector2f> compensation;
            Range<float> rotation;
        };

        Range<Eigen::Vector2f> pivot;
        std::vector<Link> links;
    };

    Eigen::Affine2f transform = Eigen::Affine2f::Identity();
    Eigen::Vector2f dimensions = Eigen::Vector2f::Zero();
    Rig rig{};
};

struct MotionMetrics {
    Eigen::AlignedBox2f box;
    float length = 0.0f;
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
struct Revision {
    uint64_t revision = 1u;
};

using Unit = std::array<Sample, 2uz>;

FILTER_ITEM_DATA<Revision> revision(L"Internal::Revision");

std::array<FILTER_ITEM_DATA<void>, 10uz> persistents{{
    {L"Internal::Persistent[0]"},
    {L"Internal::Persistent[1]"},
    {L"Internal::Persistent[2]"},
    {L"Internal::Persistent[3]"},
    {L"Internal::Persistent[4]"},
    {L"Internal::Persistent[5]"},
    {L"Internal::Persistent[6]"},
    {L"Internal::Persistent[7]"},
    {L"Internal::Persistent[8]"},
    {L"Internal::Persistent[9]"},
}};

constexpr Revision kRevision{};
constexpr auto kUnitBytes = static_cast<int>(sizeof(Unit));
constexpr auto kMaxBytesPerSlot = 16000;
constexpr auto kMaxUnitsPerSlot = kMaxBytesPerSlot / kUnitBytes;
constexpr auto kSlotCount = static_cast<int>(persistents.size());
constexpr auto kUnitLimit = kSlotCount * kMaxUnitsPerSlot;

static_assert(kUnitBytes * kMaxUnitsPerSlot <= kMaxBytesPerSlot);
static_assert(alignof(Sample) == 4uz);
static_assert(sizeof(Sample) == 32uz);
static_assert(alignof(Unit) == 4uz);
static_assert(sizeof(Unit) == 64uz);
}  // namespace internal
}  // namespace properties

[[nodiscard]] constexpr float ToRadians(float deg) noexcept {
    constexpr float f = std::numbers::pi_v<float> / 180.0f;
    return deg * f;
}

[[nodiscard]] inline Eigen::Vector2f Lerp(const Eigen::Vector2f& a, const Eigen::Vector2f& b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] std::vector<Object::Range<Object::Rotation>> BuildRotations(const Object& object, float step) {
    const auto& rig = object.rig;

    std::vector<Object::Range<Object::Rotation>> rotations(rig.links.size());

    for (size_t i = 0uz; i < rig.links.size(); ++i) {
        const auto& rotation = rig.links[i].rotation;

        const float origin = rotation.origin + (rotation.extent * step * 0.5f);
        const float extent = rotation.extent * step;

        rotations[i] = {
            .origin =
                {
                    .cos = std::cos(origin),
                    .sin = std::sin(origin),
                },
            .extent =
                {
                    .cos = std::cos(extent),
                    .sin = std::sin(extent),
                },
        };
    }

    return rotations;
}

[[nodiscard]] std::optional<std::vector<EFFECT_HANDLE>> GetEmptyHandles(int frame, const FILTER_PROC_VIDEO* ctx) {
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
                    aul::logger::Error(std::format(L"Failed to get object alias at layer {}, frame {}", i + 1, frame));
                    return std::nullopt;
                }

                const std::u8string_view object{string::AsUTF8(alias)};

                const auto meta_st = object.find(u8"[Object]");
                const auto empty_st = object.find(u8"[Object.0]");

                if (meta_st == std::string_view::npos || empty_st == std::string_view::npos) {
                    continue;
                }

                const auto empty = object.substr(empty_st, object.find(u8"[Object.1]") - empty_st);

                if (auto st = empty.find(u8"\n対象レイヤー数="); st != std::string_view::npos) {
                    st += sizeof(u8"\n対象レイヤー数=") - 1uz;

                    const auto range = string::ToNumber<int>(empty.substr(st, empty.find_first_of(u8"\r\n", st) - st));

                    if (!range.has_value()) {
                        aul::logger::Warning(range.error().message());
                        continue;
                    }

                    if (*range != 0 && *range < layer - i) {
                        continue;
                    }

                    handles.push_back(candidate);
                    layer = i;

                    const auto meta = object.substr(meta_st, empty_st - meta_st);

                    if (st = meta.find(u8"\ngroup.control="); st != std::string_view::npos) {
                        st += sizeof(u8"\ngroup.control=") - 1uz;

                        if (meta.substr(st, meta.find_first_of(u8"\r\n", st) - st) == u8"0") {
                            break;
                        }
                    }
                }
            }
        }
    }

    return handles;
}

[[nodiscard]] std::optional<std::vector<Transform>> GetEmpties(int frame, const FILTER_PROC_VIDEO* ctx) {
    if (ctx->object->layer == 0) {
        return std::vector<Transform>{};
    }

    const auto handles = GetEmptyHandles(frame, ctx);

    if (!handles.has_value()) {
        return std::nullopt;
    }

    std::vector<Transform> empties(handles->size());

    {
        const double point = static_cast<double>(frame);

        for (size_t i = 0; i < handles->size(); ++i) {
            auto* const handle = (*handles)[handles->size() - 1uz - i];

            Transform xform{};

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

[[nodiscard]] std::optional<Object::Snapshot> BuildObjectTransforms(const Sample& smp, const FILTER_PROC_VIDEO* ctx) {
    auto xforms = GetEmpties(ctx->object->frame_s + smp.frame, ctx);

    if (!xforms.has_value()) {
        return std::nullopt;
    }

    xforms->push_back(smp.transform);

    return Object::Snapshot{
        .pivot = smp.pivot,
        .transforms = std::move(*xforms),
    };
}

template <typename T>
[[nodiscard]] T ClampVelocity(const T& velocity, const T& limit) {
    return velocity.cwiseMax(limit.cwiseMin(0.0f)).cwiseMin(limit.cwiseMax(0.0f));
}

[[nodiscard]] float ClampVelocity(float velocity, float limit) {
    return std::clamp(velocity, std::min(limit, 0.0f), std::max(limit, 0.0f));
}

template <typename T, typename F>
[[nodiscard]] T Retrodict(F value_at, int order) {
    const T v0 = value_at(0uz);
    const T v1 = value_at(1uz);
    const T d0 = v1 - v0;

    if (order == 1) {
        return v0 - d0;
    }

    const T d1 = value_at(2uz) - v1;
    const T velocity = ((3.0f * d0) - d1) * 0.5f;
    const T limit = 3.0f * d0;

    return v0 - ClampVelocity(velocity, limit);
}

[[nodiscard]] std::optional<Object::Snapshot> Extrapolate(const std::vector<Sample>& samples,
                                                          const Object::Snapshot& zero, const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    static const Transform identity{};

    const int order = props::extrapolation::value;

    if (order < 1 || order > 2 || static_cast<size_t>(order) >= samples.size()) {
        return std::nullopt;
    }

    const size_t count = static_cast<size_t>(order) + 1uz;
    std::array<Object::Snapshot, 3uz> snapshots{zero};

    for (size_t i = 1uz; i < count; ++i) {
        const auto& sample = samples[i];

        if (sample.frame < 0) {
            return std::nullopt;
        }

        auto xforms = BuildObjectTransforms(sample, ctx);

        if (!xforms.has_value()) {
            return std::nullopt;
        }

        snapshots[i] = std::move(*xforms);
    }

    size_t depth = 0uz;

    for (size_t i = 0uz; i < count; ++i) {
        depth = std::max(depth, snapshots[i].transforms.size());
    }

    Object::Snapshot snapshot{};

    snapshot.pivot = Retrodict<Eigen::Vector2f>([&](size_t i) { return snapshots[i].pivot; }, order);
    snapshot.transforms.resize(depth);

    std::array<const std::vector<Transform>*, snapshots.size()> lists{};
    std::array<size_t, snapshots.size()> offsets{};

    for (size_t i = 0uz; i < snapshots.size(); ++i) {
        lists[i] = &snapshots[i].transforms;
        offsets[i] = depth - lists[i]->size();
    }

    for (size_t i = 0uz; i < depth; ++i) {
        std::array<const Transform*, snapshots.size()> inputs{};

        for (size_t j = 0uz; j < inputs.size(); ++j) {
            inputs[j] = (i < offsets[j]) ? &identity : &(*lists[j])[i - offsets[j]];
        }

        auto& xform = snapshot.transforms[i];

        xform = {
            .position = Retrodict<Eigen::Vector2f>([&](size_t k) { return inputs[k]->position; }, order),
            .scale =
                Retrodict<Eigen::Vector2f>([&](size_t k) { return inputs[k]->scale.array().log().matrix(); }, order)
                    .array()
                    .exp()
                    .matrix()
                    .cwiseMax(kEpsilon),
            .rotation = Retrodict<float>([&](size_t k) { return inputs[k]->rotation; }, order),
        };
    }

    return snapshot;
}

[[nodiscard]] std::optional<FrameMapping> BuildFrameMapping(const FILTER_PROC_VIDEO* ctx) {
    const auto span = ctx->object->frame_total - 1;
    const auto frame = std::clamp(ctx->object->frame, 0, span);

    OBJECT_IMAGE_PARAM base;

    {
        const auto spf = static_cast<double>(ctx->scene->rate) / ctx->scene->scale;
        const auto df = static_cast<double>(frame - ctx->object->frame);

        if (!ctx->get_output_image_param(nullptr, df * spf, &base, sizeof(base))) {
            aul::logger::Error(std::format(L"Failed to get object transform at layer {}, frame {}",
                                           ctx->object->layer + 1, ctx->object->frame_s + ctx->object->frame));
            return std::nullopt;
        }
    }

    Eigen::Vector2f scale(base.sx * ctx->param->sx, base.sy * ctx->param->sy);

    if ((scale.array() < 0.0f).any()) {
        aul::logger::Warning(L"Negative scaling is not supported");
    }

    return FrameMapping{
        .frame = std::clamp(ctx->object->origin_frame - ctx->object->frame_s, 0, span),
        .sample =
            {
                .pivot = Eigen::Vector2f(base.cx + ctx->param->cx, base.cy + ctx->param->cy),
                .transform =
                    {
                        .position = Eigen::Vector2f(base.x + ctx->param->x, base.y + ctx->param->y),
                        .scale = scale.cwiseMax(kEpsilon),
                        .rotation = ToRadians(base.rz + ctx->param->rz),
                    },
                .frame = frame,
            },
    };
}

void UpdatePersistent(int pos, const Sample& smp, const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    const auto r = std::div(ctx->object->index, props::internal::kMaxUnitsPerSlot);

    if (r.quot >= props::internal::kSlotCount) {
        return;
    }

    const auto& slot = props::internal::persistents[r.quot];

    if ((r.rem + 1) * props::internal::kUnitBytes > slot.size) {
        return;
    }

    static_cast<props::internal::Unit*>(slot.value)[r.rem][pos] = smp;
}

void ResetPersistent(const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    if (ctx->object->num > props::internal::kUnitLimit) {
        aul::logger::Warning(L"Object index exceeds the cache limit");

        for (auto& data : props::internal::persistents) {
            ctx->set_filter_item_data_size(&data, 0);
        }

        return;
    }

    const auto r = std::div(ctx->object->num, props::internal::kMaxUnitsPerSlot);

    int i = 0;

    for (; i < r.quot; ++i) {
        auto& data = props::internal::persistents[i];

        ctx->set_filter_item_data_size(&data, props::internal::kMaxBytesPerSlot);

        auto* dst = static_cast<props::internal::Unit*>(data.value);

        for (int j = 0; j < props::internal::kMaxUnitsPerSlot; ++j) {
            dst[j] = props::internal::Unit{};
        }
    }

    if (r.rem != 0) {
        auto& data = props::internal::persistents[i];

        ctx->set_filter_item_data_size(&data, props::internal::kUnitBytes * r.rem);

        auto* dst = static_cast<props::internal::Unit*>(data.value);

        for (int j = 0; j < r.rem; ++j) {
            dst[j] = props::internal::Unit{};
        }

        ++i;
    }

    for (; i < props::internal::kSlotCount; ++i) {
        ctx->set_filter_item_data_size(&props::internal::persistents[i], 0);
    }
}

void RestoreCache(std::vector<State>& states, const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    states.clear();

    if (props::internal::revision.value->revision != props::internal::kRevision.revision) {
        for (auto& data : props::internal::persistents) {
            ctx->set_filter_item_data_size(&data, 0);
        }

        *props::internal::revision.value = props::internal::kRevision;
        return;
    }

    states.reserve(ctx->object->num);

    for (int i = 0; i < props::internal::kSlotCount; ++i) {
        const auto& slot = props::internal::persistents[i];

        if (slot.size == 0) {
            break;
        }

        const auto count = slot.size / props::internal::kUnitBytes;

        for (int j = 0; j < count; ++j) {
            const auto& unit = static_cast<const props::internal::Unit*>(slot.value)[j];

            states.emplace_back();
            auto& state = states.back();
            auto& samples = state.samples;

            samples.assign(ctx->object->frame_total, Sample{});

            for (size_t k = 0uz; k < unit.size(); ++k) {
                const auto& smp = unit[k];

                if (smp.frame >= 0) {
                    samples[k + 1uz] = smp;
                }
            }
        }
    }
}

[[nodiscard]] const Instance* UpdateCache(const FILTER_PROC_VIDEO* ctx) {
    auto* const instance = static_cast<Instance*>(ctx->userdata);

    if (!instance->is_restored) {
        RestoreCache(instance->states, ctx);
        instance->is_restored = true;
        aul::logger::Debug(std::format(L"Restored {} states", instance->states.size()));
    }

    if (instance->states.size() != static_cast<size_t>(ctx->object->num)) {
        instance->states.assign(ctx->object->num, State{});
        ResetPersistent(ctx);
        aul::logger::Debug(L"Reset states due to object count change");
    }

    auto& state = instance->states[ctx->object->index];

    auto& curr = state.history[1uz];
    auto& prev = state.history[0uz];

    auto& samples = state.samples;
    samples.resize(ctx->object->frame_total, Sample{});

    {
        auto mapping = BuildFrameMapping(ctx);

        if (!mapping.has_value()) {
            return nullptr;
        }

        if (curr.frame >= 0 && curr.frame != mapping->frame) {
            prev.frame = curr.frame;

            if (prev.sample.frame < 0 || curr.sample.frame != mapping->sample.frame) {
                prev.sample = curr.sample;
            }
        }

        curr = std::move(*mapping);
    }

    // この時点で curr.frame は 0 以上

    if (curr.frame <= 0) {
        prev = FrameMapping{};
    } else if (curr.frame <= 2) {
        UpdatePersistent(curr.frame - 1, curr.sample, ctx);
    }

    samples[curr.frame] = curr.sample;

    if (prev.frame >= 0 && curr.frame >= 1 && prev.frame != curr.frame) {
        if (auto df = curr.frame - prev.frame; df != 1) {
            aul::logger::Warning(std::format(L"Non-consecutive frames are not supported: expected {}, got {}",
                                             prev.frame + 1, curr.frame));

            if (const auto& smp = samples[curr.frame - 1]; smp.frame >= 0) {
                prev = {
                    .frame = curr.frame - 1,
                    .sample = smp,
                };

                aul::logger::Info(L"Replaced previous frame with a recorded sample");

                return instance;
            }

            const auto span = ctx->object->frame_total - 1;
            df = std::clamp(df, -curr.frame, span - curr.frame);
            const auto t = 1.0f / static_cast<float>(df);

            const auto frame = std::lerp(static_cast<double>(curr.frame), static_cast<double>(prev.frame), 1.0 / df);

            prev = {
                .frame = curr.frame - 1,
                .sample =
                    {
                        .pivot = Lerp(curr.sample.pivot, prev.sample.pivot, t),
                        .transform =
                            {
                                Lerp(curr.sample.transform.position, prev.sample.transform.position, t),
                                Lerp(curr.sample.transform.scale, prev.sample.transform.scale, t).cwiseMax(kEpsilon),
                                std::lerp(curr.sample.transform.rotation, prev.sample.transform.rotation, t),
                            },
                        .frame = std::clamp(static_cast<int>(frame), 0, span),
                    },
            };

            aul::logger::Info(L"Replaced previous frame with an estimate");
        }
    }

    return instance;
}

[[nodiscard]] std::optional<Object> ResolveObject(const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    const float angle = static_cast<float>(props::shutter::angle.value);
    const float amount = std::max(angle / 360.0f, 0.0f);
    const float phase = static_cast<float>(props::shutter::phase.value) / angle;

    Object object;

    auto& rig = object.rig;
    Object::Snapshot start, end;

    const auto* instance = UpdateCache(ctx);

    if (instance == nullptr) {
        return std::nullopt;
    }

    const auto& state = instance->states[ctx->object->index];

    if (const auto& curr = state.history[1uz]; curr.frame >= 0) {
        if (auto xforms = BuildObjectTransforms(curr.sample, ctx); xforms.has_value()) {
            start = std::move(*xforms);
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    if (ctx->object->origin_frame == ctx->object->frame_s) {
        if (props::extrapolation::value > 0) {
            if (auto xforms = Extrapolate(state.samples, start, ctx); xforms.has_value()) {
                end = std::move(*xforms);
            } else {
                aul::logger::Warning(L"No cached frame available for extrapolation");
                end = start;
            }
        } else {
            end = start;
        }
    } else if (const auto& prev = state.history[0uz].sample; prev.frame >= 0) {
        if (auto xforms = BuildObjectTransforms(prev, ctx); xforms.has_value()) {
            end = std::move(*xforms);
        } else {
            return std::nullopt;
        }
    } else {
        aul::logger::Warning(L"No cached frame available");
        end = start;
    }

    object.dimensions = Eigen::Vector2i(ctx->object->width, ctx->object->height).cast<float>();

    {
        const Eigen::Vector2f center = object.dimensions * 0.5f;
        start.pivot += center;
        end.pivot += center;
    }

    const auto n = std::ssize(start.transforms) - std::ssize(end.transforms);

    if (n > 0) {
        end.transforms.insert(end.transforms.begin(), n, Transform{});
    } else if (n < 0) {
        start.transforms.insert(start.transforms.begin(), -n, Transform{});
    }

    const size_t depth = start.transforms.size();

    rig.links.resize(depth);

    end.pivot = Lerp(start.pivot, end.pivot, amount);
    rig.pivot = {
        .origin = start.pivot + (end.pivot - start.pivot) * phase,
        .extent = end.pivot - start.pivot,
    };

    for (size_t i = 0uz; i < depth; ++i) {
        const auto& st = start.transforms[i];
        auto& ed = end.transforms[i];

        object.transform = object.transform * Eigen::Translation2f(st.position) * Eigen::Rotation2Df(st.rotation) *
                           Eigen::Scaling(st.scale);

        const Eigen::Vector2f compensation_st = st.scale.cwiseInverse();
        const Eigen::Vector2f compensation_ed = Lerp(compensation_st, ed.scale.cwiseInverse(), amount);
        const Eigen::Vector2f compensation_shift = (compensation_ed - compensation_st) * phase;
        const Eigen::Vector2f compensation_origin = (compensation_st + compensation_shift).cwiseMax(kEpsilon);

        ed.position = Lerp(st.position, ed.position, amount);
        ed.rotation = std::lerp(st.rotation, ed.rotation, amount);

        rig.links[i] = {
            .position =
                {
                    .origin = st.position + (ed.position - st.position) * phase,
                    .extent = ed.position - st.position,
                },
            .compensation =
                {
                    .origin = compensation_origin,
                    .extent = (compensation_ed + compensation_shift).cwiseMax(kEpsilon) - compensation_origin,
                },
            .rotation =
                {
                    .origin = st.rotation + ((ed.rotation - st.rotation) * phase),
                    .extent = ed.rotation - st.rotation,
                },
        };
    }

    object.transform = object.transform * Eigen::Translation2f(-start.pivot);

    return object;
}

[[nodiscard]] MotionMetrics ComputeMotionMetrics(const Object& object, int samples) {
    const auto& rig = object.rig;

    const float step = 1.0f / static_cast<float>(samples);
    const auto base_to_world = object.transform.inverse();
    auto rotations = BuildRotations(object, step);

    const std::array<Eigen::Vector2f, 4uz> corners = {{
        Eigen::Vector2f::Zero(),
        {object.dimensions.x(), 0.0f},
        {0.0f, object.dimensions.y()},
        object.dimensions,
    }};

    Eigen::AlignedBox2f box(Eigen::Vector2f::Zero(), object.dimensions);
    std::array<Eigen::Vector2f, 4uz> prev{};
    std::array<float, 4uz> paths{};
    float len = 0.0f;

    for (int i = 0; i <= samples; ++i) {
        const auto t = step * static_cast<float>(i);

        Eigen::Affine2f sample_to_base = base_to_world;

        for (size_t j = 0uz; j < rig.links.size(); ++j) {
            const auto& link = rig.links[j];
            auto& rotation = rotations[j];

            const Eigen::Vector2f position = link.position.origin + link.position.extent * t;
            const Eigen::Vector2f scale = (link.compensation.origin + link.compensation.extent * t).cwiseInverse();

            Eigen::Matrix2f linear;
            linear << rotation.origin.cos * scale.x(), -rotation.origin.sin * scale.y(),
                rotation.origin.sin * scale.x(), rotation.origin.cos * scale.y();

            sample_to_base.translation() += sample_to_base.linear() * position;
            sample_to_base.linear() *= linear;

            rotation.origin = {
                .cos = (rotation.origin.cos * rotation.extent.cos) - (rotation.origin.sin * rotation.extent.sin),
                .sin = (rotation.origin.sin * rotation.extent.cos) + (rotation.origin.cos * rotation.extent.sin),
            };
        }

        const Eigen::Vector2f pivot = rig.pivot.origin + rig.pivot.extent * t;

        const Eigen::Vector2f origin = sample_to_base * -pivot;
        const auto linear = sample_to_base.linear();

        std::array<Eigen::Vector2f, 4uz> curr{};
        for (size_t j = 0uz; j < corners.size(); ++j) {
            curr[j] = origin + linear * corners[j];
            box.extend(curr[j]);

            if (i > 0) {
                paths[j] += (curr[j] - prev[j]).norm();
                len = std::max(len, paths[j]);
            }
        }

        prev = curr;
    }

    return {
        .box = box,
        .length = std::isfinite(len) ? len : std::numeric_limits<float>::max(),
    };
}

void CreateTrajectory(const Object& object, int samples, std::vector<renderer::Float2x3>& trajectory) {
    const auto& rig = object.rig;

    const float step = 1.0f / static_cast<float>(samples);
    auto rotations = BuildRotations(object, step);
    trajectory.resize(samples);

    for (int i = 0; i < samples; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) * step;
        Eigen::Affine2f node = Eigen::Affine2f::Identity();

        for (size_t j = 0uz; j < rig.links.size(); ++j) {
            const auto& link = rig.links[j];
            auto& rotation = rotations[j];

            const Eigen::Vector2f position = link.position.origin + link.position.extent * t;
            const Eigen::Vector2f compensation = link.compensation.origin + link.compensation.extent * t;

            Eigen::Matrix2f linear;
            linear << compensation.x() * rotation.origin.cos, compensation.x() * rotation.origin.sin,
                -compensation.y() * rotation.origin.sin, compensation.y() * rotation.origin.cos;

            node.translation() = linear * (node.translation() - position);
            node.linear() = linear * node.linear();

            rotation.origin = {
                .cos = (rotation.origin.cos * rotation.extent.cos) - (rotation.origin.sin * rotation.extent.sin),
                .sin = (rotation.origin.sin * rotation.extent.cos) + (rotation.origin.cos * rotation.extent.sin),
            };
        }

        node.translation() += rig.pivot.origin + rig.pivot.extent * t;

        trajectory[i] = {{
            {node(0, 0), node(0, 1), node(0, 2)},
            {node(1, 0), node(1, 1), node(1, 2)},
        }};
    }
}

bool Apply(FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    if (ctx->object->index < 0 || ctx->object->index >= ctx->object->num) {
        aul::logger::Warning(L"Unable to determine object count");
        return true;
    }

    if (ctx->object->width <= 0 || ctx->object->height <= 0) {
        return false;
    }

    if (props::shutter::angle.value < kEpsilon) {
        return true;
    }

    const auto object = ResolveObject(ctx);

    if (!object.has_value()) {
        return false;
    }

    const auto metrics = ComputeMotionMetrics(*object, 64);
    const int required_samples = static_cast<int>(std::min(std::ceil(metrics.length) + 1.0f, 65536.0f));

    if (required_samples < 2) {
        return true;
    }

    const int32_t limit = aul::context::GetEditorState() == aul::context::EditorState::kExporting
                              ? static_cast<int32_t>(props::sampling::render::sample_limit.value)
                              : static_cast<int32_t>(props::sampling::viewport::sample_limit.value);

    const int32_t samples = std::min(limit, required_samples);

    if (samples < 2) {
        return true;
    }

    {
        Eigen::Vector2f origin;
        Eigen::Vector2f size;

        if (props::should_resize.value) {
            origin = metrics.box.min();
            size = metrics.box.sizes().array().ceil();
            Eigen::Map<Eigen::Vector2f>(&ctx->param->cx) -= origin + (size - object->dimensions) * 0.5f;
        } else {
            origin = Eigen::Vector2f::Zero();
            size = object->dimensions;
        }

        if (!ctx->copy_image_resource(L"resource:image", nullptr)) {
            aul::logger::Error(L"Failed to copy image 'object' to 'resource:image'");
            return false;
        }

        ctx->set_image_data(nullptr, static_cast<int>(size.x()), static_cast<int>(size.y()));

        auto* const dst = ctx->get_image_texture2d();
        auto* const img = ctx->get_image_resource_texture2d(L"resource:image");

        if (img == nullptr || dst == nullptr) {
            aul::logger::Error(L"Failed to get 'ID3D11Texture2D' pointers");
            return false;
        }

        thread_local std::vector<renderer::Float2x3> trajectory;
        CreateTrajectory(*object, samples, trajectory);

        const float mix = std::clamp(static_cast<float>(props::compositing::mix.value) * 0.01f, 0.0f, 1.0f) * 2.0f;
        const float falloff = std::max(static_cast<float>(props::compositing::falloff.value) * 0.01f, 0.0f);
        const float decay = std::pow(std::max(1.0f - falloff, kEpsilon), 1.0f / static_cast<float>(samples));

        const renderer::Target target{
            .image = img,
            .trajectory = trajectory,
        };

        const renderer::Parameter param{
            .transform =
                {

                    {
                        object->transform(0, 0),
                        object->transform(0, 1),
                        object->transform(0, 2),
                    },
                    {
                        object->transform(1, 0),
                        object->transform(1, 1),
                        object->transform(1, 2),
                    },
                },
            .origin =
                {
                    origin.x(),
                    origin.y(),
                },
            .texel =
                {
                    1.0f / object->dimensions.x(),
                    1.0f / object->dimensions.y(),
                },
            .mix =
                {
                    std::min(2.0f - mix, 1.0f),
                    std::min(mix, 1.0f),
                },
            .decay = decay,
            .samples = samples,
        };

        const auto ec = renderer::Render(dst, [&target, &param](const renderer::Context& ctx) -> std::error_code {
            return ctx.Draw(target, param);
        });

        if (ec != std::error_code{}) {
            aul::logger::Error(ec.message());
            return false;
        }
    }

    if (props::should_print_diagnostics.value) {
        const auto* const instance = static_cast<const Instance*>(ctx->userdata);
        const auto memory =
            sizeof(Instance) + (instance->states.size() * sizeof(State)) + (ctx->object->frame_total * sizeof(Sample));

        aul::logger::Info(
            std::format(L"\n"
                        L"Effect ID       : {}\n"
                        L"Index           : {}\n"
                        L"Required Samples: {}\n"
                        L"Samples         : {}\n"
                        L"Memory          : {} Bytes\n",
                        ctx->object->effect_id, ctx->object->index, required_samples, samples, memory));
    }

    return true;
}

void* Init([[maybe_unused]] int64_t id) { return new Instance{}; }

void Deinit([[maybe_unused]] int64_t id, void* instance) { delete static_cast<Instance*>(instance); }

inline constinit auto props = []<std::size_t... Is>(std::index_sequence<Is...>) {
    return std::to_array<void*>({
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
        &properties::internal::revision,
        (&properties::internal::persistents[Is])...,
        nullptr,
    });
}(std::make_index_sequence<std::size(properties::internal::persistents)>{});

constinit FILTER_PLUGIN_TABLE desc{
    .flag = FILTER_PLUGIN_TABLE::FLAG_VIDEO | FILTER_PLUGIN_TABLE::FLAG_USERDATA,
    .name = L"ObjectMotionBlur_LK",
    .label = L"ぼかし",
    .information = L"ObjectMotionBlur_LK v" VERSION L" by Korarei",
    .items = props.data(),
    .func_proc_video = Apply,
    .func_proc_audio = nullptr,
    .func_create = Init,
    .func_destroy = Deinit,
};
}  // namespace

namespace blur::object {
void Register(HOST_APP_TABLE* host) {
    host->register_filter_plugin(&desc);

    host->register_clear_cache_handler([]([[maybe_unused]] EDIT_SECTION* edit) { renderer::Reset(); });
}

void Unregister() { renderer::Reset(); }
}  // namespace blur::object
