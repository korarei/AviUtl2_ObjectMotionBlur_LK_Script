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

        const float origin = rotation.origin;
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
    auto xforms = GetEmpties(std::min(ctx->object->frame_s + smp.frame, ctx->object->frame_e), ctx);

    if (!xforms.has_value()) {
        return std::nullopt;
    }

    xforms->push_back(smp.transform);

    return Object::Snapshot{
        .pivot = smp.pivot,
        .transforms = std::move(*xforms),
    };
}

[[nodiscard]] std::optional<Object::Snapshot> Extrapolate(const std::array<std::optional<FrameMapping>, 4uz>& frames,
                                                          const Object::Snapshot& zero, const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    static const Transform identity{};

    if (props::extrapolation::value < 1 || props::extrapolation::value > 2) {
        return std::nullopt;
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

    const auto retrodict_scalar = [](auto&& value_at) -> float {
        const float v0 = value_at(0uz);
        const float v1 = value_at(1uz);
        const float d0 = v1 - v0;

        if (props::extrapolation::value == 1) {
            return v0 - d0;
        }

        const float v2 = value_at(2uz);
        const float d1 = v2 - v1;

        const float velocity = ((3.0f * d0) - d1) * 0.5f;
        const float limit = 3.0f * d0;

        return v0 - std::clamp(velocity, std::min(limit, 0.0f), std::max(limit, 0.0f));
    };

    std::array<Object::Snapshot, 3uz> snapshots{zero};

    for (int i = 1; i <= props::extrapolation::value; ++i) {
        const auto& frame = frames[i + 1];

        if (!frame.has_value()) {
            return std::nullopt;
        }

        auto xforms = BuildObjectTransforms(frame->sample, ctx);

        if (!xforms.has_value()) {
            return std::nullopt;
        }

        snapshots[i] = std::move(*xforms);
    }

    size_t depth = 0uz;

    for (int i = 0; i <= props::extrapolation::value; ++i) {
        depth = std::max(depth, snapshots[i].transforms.size());
    }

    Object::Snapshot snapshot{};

    snapshot.pivot = retrodict([&](size_t i) { return snapshots[i].pivot.array(); });
    snapshot.transforms.resize(depth);

    std::array<const std::vector<Transform>*, 3uz> lists{};
    std::array<size_t, 3uz> offsets{};

    for (int i = 0; i <= props::extrapolation::value; ++i) {
        lists[i] = &snapshots[i].transforms;
        offsets[i] = depth - lists[i]->size();
    }

    for (size_t i = 0uz; i < depth; ++i) {
        std::array<const Transform*, 3uz> inputs{};

        for (int j = 0; j <= props::extrapolation::value; ++j) {
            inputs[j] = (i < offsets[j]) ? &identity : &(*lists[j])[i - offsets[j]];
        }

        auto& xform = snapshot.transforms[i];

        xform.position = retrodict([&](size_t k) { return inputs[k]->position.array(); });

        xform.scale = retrodict([&](size_t k) { return inputs[k]->scale.array().log(); }).exp().cwiseMax(kEpsilon);

        xform.rotation = retrodict_scalar([&](size_t k) { return inputs[k]->rotation; });
    }

    return snapshot;
}

[[nodiscard]] std::optional<FrameMapping> BuildFrameMapping(const FILTER_PROC_VIDEO* ctx) {
    const auto frame = std::max(ctx->object->frame, 0);

    OBJECT_IMAGE_PARAM base;

    {
        const auto spf = static_cast<double>(ctx->scene->rate) / ctx->scene->scale;
        const auto df = static_cast<double>(std::min(frame, ctx->object->frame_total - 1) - ctx->object->frame);

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
        .frame = std::max(ctx->object->origin_frame - ctx->object->frame_s, 0),
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

[[nodiscard]] Sample LerpSample(const Sample& curr, const Sample& prev, float t) {
    return {
        .pivot = Lerp(curr.pivot, prev.pivot, t),
        .transform =
            {
                Lerp(curr.transform.position, prev.transform.position, t),
                Lerp(curr.transform.scale, prev.transform.scale, t),
                std::lerp(curr.transform.rotation, prev.transform.rotation, t),
            },
        .frame = static_cast<int>(
            std::lerp(static_cast<double>(curr.frame), static_cast<double>(prev.frame), static_cast<double>(t))),
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

void RestoreCache(std::vector<std::array<std::optional<FrameMapping>, 4uz>>& mappings, const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    mappings.clear();

    if (props::internal::revision.value->revision != props::internal::kRevision.revision) {
        for (auto& data : props::internal::persistents) {
            ctx->set_filter_item_data_size(&data, 0);
        }

        *props::internal::revision.value = props::internal::kRevision;
        return;
    }

    mappings.reserve(ctx->object->num);

    for (int i = 0; i < props::internal::kSlotCount; ++i) {
        const auto& slot = props::internal::persistents[i];

        if (slot.size == 0) {
            break;
        }

        const auto count = slot.size / props::internal::kUnitBytes;

        for (int j = 0; j < count; ++j) {
            const auto& unit = static_cast<const props::internal::Unit*>(slot.value)[j];

            mappings.emplace_back();
            auto& frames = mappings.back();

            for (size_t k = 0uz; k < unit.size(); ++k) {
                const auto& smp = unit[k];

                if (smp.frame >= 0) {
                    frames[k + 2uz] = {
                        .frame = -1,
                        .sample = smp,
                    };
                }
            }
        }
    }
}

[[nodiscard]] const Instance& UpdateCache(const FILTER_PROC_VIDEO* ctx) {
    auto* const instance = static_cast<Instance*>(ctx->userdata);

    if (!instance->is_restored) {
        RestoreCache(instance->mappings, ctx);
        instance->is_restored = true;
        aul::logger::Debug(std::format(L"Restored {} mappings", instance->mappings.size()));
    }

    if (instance->mappings.size() != static_cast<size_t>(ctx->object->num)) {
        instance->mappings.assign(ctx->object->num, std::array<std::optional<FrameMapping>, 4uz>{});
        ResetPersistent(ctx);
        aul::logger::Debug(L"Reset mappings due to object count change");
    }

    auto& frames = instance->mappings[ctx->object->index];
    auto& prev = frames[0uz];
    auto& curr = frames[1uz];

    {
        const auto mapping = BuildFrameMapping(ctx);

        if (!mapping.has_value()) {
            curr = std::nullopt;
            return *instance;
        }

        if (curr.has_value() && curr->sample.frame != mapping->sample.frame) {
            prev = curr;
        }

        curr = mapping;
    }

    if (curr->frame == 0) {
        prev = std::nullopt;
    } else {
        if (curr->frame <= static_cast<int>(frames.size()) - 2) {
            frames[curr->frame + 1] = curr;
            UpdatePersistent(curr->frame - 1, curr->sample, ctx);
        }

        if (prev.has_value() && prev->sample.frame != curr->sample.frame && prev->frame != curr->frame) {
            if (auto df = curr->frame - prev->frame; df != 0 && df != 1) {
                df = std::clamp(df, -curr->frame, ctx->object->frame_total - 1 - curr->frame);

                const float t = 1.0f / static_cast<float>(df);

                prev->frame = curr->frame - 1;
                prev->sample = LerpSample(curr->sample, prev->sample, t);

                if (df < 0) {
                    aul::logger::Warning(L"Reverse playback is not supported");
                }
            }
        }
    }

    return *instance;
}

[[nodiscard]] std::optional<Object> ResolveObject(const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    const float angle = static_cast<float>(props::shutter::angle.value);
    const float amount = std::max(angle / 360.0f, 0.0f);
    const float phase = static_cast<float>(props::shutter::phase.value) / angle;

    Object object;

    auto& rig = object.rig;
    Object::Snapshot start, end;

    const auto& frames = UpdateCache(ctx).mappings[ctx->object->index];

    if (const auto& curr = frames[1uz]; curr.has_value()) {
        if (auto xforms = BuildObjectTransforms(curr->sample, ctx); xforms.has_value()) {
            start = std::move(*xforms);
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    if (ctx->object->origin_frame == ctx->object->frame_s) {
        if (props::extrapolation::value > 0) {
            if (auto xforms = Extrapolate(frames, start, ctx); xforms.has_value()) {
                end = std::move(*xforms);
            } else {
                aul::logger::Warning(L"No cached frame available for extrapolation");
                end = start;
            }
        } else {
            end = start;
        }
    } else if (const auto& prev = frames[0uz]; prev.has_value()) {
        if (auto xforms = BuildObjectTransforms(prev->sample, ctx); xforms.has_value()) {
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

    const float step = 1.0f / static_cast<float>(samples - 1);
    auto rotations = BuildRotations(object, step);
    trajectory.resize(samples);

    for (int i = 0; i < samples; ++i) {
        const float t = step * static_cast<float>(i);
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
        const float decay = std::pow(std::max(1.0f - falloff, kEpsilon), 1.0f / static_cast<float>(samples - 1));

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
