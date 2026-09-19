#include "../object.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
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

template <size_t N, typename T = Transform>
class Table {
  public:
    explicit Table() = default;
    explicit Table(size_t size) : data_(size * N) {}

    void Reserve(size_t size) { data_.reserve(size * N); }

    void Push(std::span<const T, N> data) { data_.append_range(data); }

    [[nodiscard]] size_t GetSize() const noexcept { return data_.size() / N; }

    [[nodiscard]] std::span<T, N> operator[](size_t row) noexcept { return std::span<T, N>(&data_[row * N], N); }

    [[nodiscard]] std::span<const T, N> operator[](size_t row) const noexcept {
        return std::span<const T, N>(&data_[row * N], N);
    }

    [[nodiscard]] T& operator[](size_t row, size_t col) noexcept { return data_[col + (row * N)]; }

    [[nodiscard]] const T& operator[](size_t row, size_t col) const noexcept { return data_[col + (row * N)]; }

  private:
    std::vector<T> data_;
};

template <typename T>
struct Segment {
    T origin;
    T extent;
};

struct Rotor {
    float cos;
    float sin;
};

struct Object {
    template <size_t N>
    struct Trace {
        std::array<Eigen::Vector2f, N> pivots{};
        Table<N, Transform> transforms{};
    };

    struct Rig {
        struct Link {
            Segment<Eigen::Vector2f> position;
            Segment<Eigen::Vector2f> compensation;
            Segment<float> rotation;
        };

        Segment<Eigen::Vector2f> pivot;
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
namespace tint {
FILTER_ITEM_GROUP name(L"Tint", false);
namespace source {
FILTER_ITEM_SELECT::ITEM contents[] = {
    {L"Image", 0},
    {L"Layer", 1},
    {nullptr, -1},
};
FILTER_ITEM_SELECT control(L"Tint::Source", 0, contents);
auto& value = control.value;
}  // namespace source
FILTER_ITEM_FILE image(
    L"Tint::Image", L"",
    L"Image Files (*.bmp;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.webp)\0*.bmp;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.webp\0\0");
FILTER_ITEM_TRACK layer(L"Tint::Layer", 0, -100, 100, 1, L"---");
namespace visibility {
namespace image_selected {
FILTER_ITEM_HIDE_RULE image(L"Tint::Image", L"Tint::Source", FILTER_ITEM_HIDE_RULE::OPERATOR::NOT_EQUAL, 0);
}  // namespace image_selected
namespace layer_selected {
FILTER_ITEM_HIDE_RULE layer(L"Tint::Layer", L"Tint::Source", FILTER_ITEM_HIDE_RULE::OPERATOR::NOT_EQUAL, 1);
}  // namespace layer_selected
}  // namespace visibility
}  // namespace tint
namespace compositing {
FILTER_ITEM_GROUP name(L"Compositing", false);
FILTER_ITEM_TRACK mix(L"Compositing::Mix", 100.0, 0.0, 100.0, 0.01);
namespace alpha_mode {
FILTER_ITEM_SELECT::ITEM contents[] = {
    {L"Alpha Blending", 0},
    {L"Alpha Hashed", 1},
    {nullptr, -1},
};
FILTER_ITEM_SELECT control(L"Compositing::Alpha Mode", 0, contents);
auto& value = control.value;
}  // namespace alpha_mode
}  // namespace compositing
FILTER_ITEM_GROUP additional_options(L"Additional Options", false);
namespace extrapolation {
FILTER_ITEM_SELECT::ITEM contents[] = {{L"None", 0}, {L"Linear", 1}, {L"Quadratic", 2}, {nullptr, -1}};
FILTER_ITEM_SELECT control(L"Extrapolation", 2, contents);
auto& value = control.value;
}  // namespace extrapolation
namespace layer_reference {
FILTER_ITEM_SELECT::ITEM contents[] = {
    {L"Absolute", 0},
    {L"Relative", 1},
    {nullptr, -1},
};
FILTER_ITEM_SELECT control(L"Layer Reference", 0, contents);
auto& value = control.value;
}  // namespace layer_reference
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

[[nodiscard]] std::vector<Segment<Rotor>> BuildRotors(const Object& object, float step) {
    const auto& rig = object.rig;

    std::vector<Segment<Rotor>> rotors(rig.links.size());

    for (size_t i = 0uz; i < rig.links.size(); ++i) {
        const auto& rotation = rig.links[i].rotation;

        const float origin = rotation.origin + (rotation.extent * step * 0.5f);
        const float extent = rotation.extent * step;

        rotors[i] = {
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

    return rotors;
}

template <size_t N>
[[nodiscard]] Table<N, EFFECT_HANDLE> GetEmptyHandles(const std::array<int, N>& frames, const FILTER_PROC_VIDEO* ctx) {
    Table<N, EFFECT_HANDLE> handles{};

    if (ctx->object->layer <= 0) {
        return handles;
    }

    {
        auto* const object_handle = ctx->get_image_object(ctx->object->layer, 0.0);
        if (object_handle == nullptr || !ctx->edit->get_object_flag(object_handle, OBJECT_FLAG_TYPE::ENABLE_GROUP)) {
            return handles;
        }
    }

    handles.Reserve(ctx->object->layer);

    {
        std::array<int, N> targets;
        targets.fill(ctx->object->layer - 1);
        std::array<int, N> cursors = targets;

        while (true) {
            std::array<EFFECT_HANDLE, N> row{};

            for (size_t i = 0uz; i < N; ++i) {
                const auto frame = frames[i];
                auto& target = targets[i];
                auto& cursor = cursors[i];

                for (; cursor >= 0; --cursor) {
                    if (!ctx->edit->get_layer_enable(cursor)) {
                        continue;
                    }

                    auto* const object_handle = ctx->edit->find_object(cursor, frame);

                    if (object_handle == nullptr || ctx->edit->get_object_layer_frame(object_handle).start > frame) {
                        continue;
                    }

                    auto* const candidate = ctx->edit->find_effect(object_handle, L"グループ制御");

                    if (candidate == nullptr || !ctx->edit->get_effect_enable(candidate)) {
                        continue;
                    }

                    const auto* const alias = ctx->edit->get_effect_item_value(candidate, L"対象レイヤー数");

                    if (alias == nullptr) {
                        aul::logger::Warning(std::format(
                            L"Failed to get 'グループ制御:対象レイヤー数' at layer {}, frame {}", cursor + 1, frame));
                        continue;
                    }

                    const auto range = string::ToNumber<int>(std::string_view{alias});

                    if (!range.has_value()) {
                        aul::logger::Warning(range.error().message());
                        continue;
                    }

                    if (*range != 0 && *range < target - cursor) {
                        continue;
                    }

                    row[i] = candidate;
                    target = cursor;

                    if (ctx->edit->get_object_flag(object_handle, OBJECT_FLAG_TYPE::ENABLE_GROUP)) {
                        --cursor;
                    } else {
                        cursor = -1;
                    }

                    break;
                }
            }

            if (std::ranges::none_of(row, [](auto* handle) { return handle; })) {
                break;
            }

            handles.Push(row);
        }
    }

    return handles;
}

template <typename C>
[[nodiscard]] inline auto BuildObjectTrace(const C& samples, const FILTER_PROC_VIDEO* ctx) {
    constexpr size_t n = [] {
        using D = std::decay_t<C>;
        if constexpr (requires { D::extent; }) {
            return D::extent;
        } else {
            return std::tuple_size_v<D>;
        }
    }();

    auto sample = [&](size_t i) -> const Sample& {
        if constexpr (std::is_pointer_v<std::decay_t<decltype(samples[0])>>) {
            return *samples[i];
        } else {
            return samples[i];
        }
    };

    std::array<int, n> frames;
    std::array<Eigen::Vector2f, n> pivots;

    for (size_t i = 0uz; i < n; ++i) {
        const auto& smp = sample(i);
        frames[i] = ctx->object->frame_s + smp.frame;
        pivots[i] = smp.pivot;
    }

    const auto handles = GetEmptyHandles(frames, ctx);
    const size_t depth = handles.GetSize();

    Table<n> xforms(depth + 1uz);

    for (size_t i = 0uz; i < depth; ++i) {
        const size_t k = depth - 1uz - i;

        for (size_t j = 0uz; j < n; ++j) {
            auto* const handle = handles[k, j];

            if (handle == nullptr) {
                continue;
            }

            const double point = static_cast<double>(frames[j]);

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

            xforms[i, j] = std::move(xform);
        }
    }

    for (size_t i = 0uz; i < n; ++i) {
        xforms[depth, i] = sample(i).transform;
    }

    return Object::Trace<n>{
        .pivots = pivots,
        .transforms = std::move(xforms),
    };
}

[[nodiscard]] inline Object::Trace<2uz> DuplicateTrace(const Sample& sample, const FILTER_PROC_VIDEO* ctx) {
    const std::array samples{&sample};
    const auto trace = BuildObjectTrace(samples, ctx);
    const size_t depth = trace.transforms.GetSize();

    Table<2uz> xforms(depth);

    for (size_t i = 0uz; i < depth; ++i) {
        xforms[i, 0] = trace.transforms[i, 0];
        xforms[i, 1] = trace.transforms[i, 0];
    }

    return Object::Trace<2uz>{
        .pivots =
            {
                trace.pivots[0],
                trace.pivots[0],
            },
        .transforms = std::move(xforms),
    };
}

template <typename C>
[[nodiscard]] inline auto Retrodict(const C& values) {
    constexpr size_t n = [] {
        using D = std::decay_t<C>;
        if constexpr (requires { D::extent; }) {
            return D::extent;
        } else {
            return std::tuple_size_v<D>;
        }
    }();

    using T = std::decay_t<decltype(values[0])>;

    return [&]<size_t... Is>(std::index_sequence<Is...>) -> T {
        constexpr auto weights = [] {
            std::array<float, n> w{1.0f};
            float c = -static_cast<float>(n - 1uz);
            for (size_t i = 1uz; i < n; ++i) {
                w[0] += 1.0f / static_cast<float>(i);
                w[i] = c / static_cast<float>(i);
                c = -c * static_cast<float>(n - 1uz - i) / static_cast<float>(i + 1uz);
            }
            return w;
        }();

        auto clamp = [](const auto& v, const auto& lim) {
            if constexpr (requires { v.cwiseMax(lim); }) {
                return v.cwiseMax(lim.cwiseMin(0.0f)).cwiseMin(lim.cwiseMax(0.0f));
            } else {
                return std::clamp(v, std::min(lim, 0.0f), std::max(lim, 0.0f));
            }
        };

        if constexpr (std::is_same_v<T, Transform>) {
            return Transform{
                .position = values[0].position - clamp(values[0].position - ((values[Is].position * weights[Is]) + ...),
                                                       3.0f * (values[1].position - values[0].position)),
                .scale =
                    (values[0].scale.array().log().matrix() -
                     clamp(values[0].scale.array().log().matrix() -
                               (((values[Is].scale.array().log() * weights[Is]) + ...)).matrix(),
                           3.0f * (values[1].scale.array().log().matrix() - values[0].scale.array().log().matrix())))
                        .array()
                        .exp()
                        .matrix()
                        .cwiseMax(kEpsilon),
                .rotation = values[0].rotation - clamp(values[0].rotation - ((values[Is].rotation * weights[Is]) + ...),
                                                       3.0f * (values[1].rotation - values[0].rotation)),
            };
        } else {
            return T(values[0] - clamp(values[0] - ((values[Is] * weights[Is]) + ...), 3.0f * (values[1] - values[0])));
        }
    }(std::make_index_sequence<n>{});
}

template <size_t N>
[[nodiscard]] Object::Trace<2uz> Extrapolate(std::span<const Sample, N> samples, const FILTER_PROC_VIDEO* ctx) {
    static_assert(N >= 2uz);

    for (size_t i = 0uz; i < N; ++i) {
        if (samples[i].frame < 0) {
            aul::logger::Warning(L"No cached frame available for extrapolation");
            return DuplicateTrace(samples[0], ctx);
        }
    }

    const auto trace = BuildObjectTrace(samples, ctx);
    const size_t depth = trace.transforms.GetSize();

    Table<2uz> xforms(depth);

    for (size_t i = 0uz; i < depth; ++i) {
        xforms[i, 0] = Retrodict(trace.transforms[i]);
        xforms[i, 1] = trace.transforms[i, 0];
    }

    return Object::Trace<2uz>{
        .pivots =
            {
                Retrodict(trace.pivots),
                trace.pivots[0],
            },
        .transforms = std::move(xforms),
    };
}

[[nodiscard]] FrameMapping BuildFrameMapping(const FILTER_PROC_VIDEO* ctx) {
    const auto span = ctx->object->frame_total - 1;
    const auto frame = std::clamp(ctx->object->frame, 0, span);

    OBJECT_IMAGE_PARAM base;

    {
        const auto spf = static_cast<double>(ctx->scene->rate) / ctx->scene->scale;
        const auto df = static_cast<double>(frame - ctx->object->frame);

        if (!ctx->get_output_image_param(nullptr, df * spf, &base, sizeof(base))) {
            throw std::runtime_error(std::format("Failed to get object transform at layer {}, frame {}",
                                                 ctx->object->layer + 1, ctx->object->frame_s + ctx->object->frame));
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

[[nodiscard]] const Instance& UpdateCache(const FILTER_PROC_VIDEO* ctx) {
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

        if (curr.frame >= 0 && curr.frame != mapping.frame) {
            prev.frame = curr.frame;

            if (prev.sample.frame < 0 || curr.sample.frame != mapping.sample.frame) {
                prev.sample = curr.sample;
            }
        }

        curr = std::move(mapping);
    }

    // この時点で curr.frame は 0 以上

    if (curr.frame <= 0) {
        prev = FrameMapping{};
    } else if (curr.frame <= 2) {
        UpdatePersistent(curr.frame - 1, curr.sample, ctx);
    }

    samples[curr.frame] = curr.sample;

    if (prev.frame >= 0 && curr.frame >= 1 && prev.frame != curr.frame) {
        if (const auto df = curr.frame - prev.frame; df != 1) {
            aul::logger::Warning(std::format(L"Non-consecutive frames are not supported: expected {}, got {}",
                                             prev.frame + 1, curr.frame));

            if (const auto& smp = samples[curr.frame - 1]; smp.frame >= 0) {
                prev = {
                    .frame = curr.frame - 1,
                    .sample = smp,
                };

                aul::logger::Info(L"Replaced previous frame with a recorded sample");
            } else {
                prev = FrameMapping{};
            }
        }
    }

    return *instance;
}

[[nodiscard]] Object ResolveObject(const FILTER_PROC_VIDEO* ctx) {
    namespace props = properties;

    const float angle = static_cast<float>(props::shutter::angle.value);
    const float amount = std::max(angle / 360.0f, 0.0f);
    const float phase = static_cast<float>(props::shutter::phase.value) / angle;

    const auto trace = [&]() -> Object::Trace<2uz> {
        const auto& instance = UpdateCache(ctx);
        const auto& state = instance.states[ctx->object->index];
        const auto& curr = state.history[1uz].sample;

        if (ctx->object->origin_frame == ctx->object->frame_s) {
            switch (props::extrapolation::value) {
                case 1:
                    if (state.samples.size() >= 2uz) {
                        return Extrapolate<2uz>(std::span<const Sample, 2uz>(state.samples.data(), 2uz), ctx);
                    }
                    break;
                case 2:
                    if (state.samples.size() >= 3uz) {
                        return Extrapolate<3uz>(std::span<const Sample, 3uz>(state.samples.data(), 3uz), ctx);
                    }
                    break;
                default:
                    return DuplicateTrace(curr, ctx);
            }

            aul::logger::Warning(L"Insufficient frames for extrapolation");
            return DuplicateTrace(curr, ctx);
        }

        if (const auto& prev = state.history[0uz].sample; prev.frame >= 0) {
            const std::array samples{&prev, &curr};
            return BuildObjectTrace(samples, ctx);
        }

        aul::logger::Warning(L"No cached frame available");
        return DuplicateTrace(curr, ctx);
    }();

    Object object;
    object.dimensions = Eigen::Vector2i(ctx->object->width, ctx->object->height).cast<float>();

    const Eigen::Vector2f center = object.dimensions * 0.5f;
    const Eigen::Vector2f pivot_st = trace.pivots[1] + center;
    Eigen::Vector2f pivot_ed = trace.pivots[0] + center;

    const size_t depth = trace.transforms.GetSize();
    auto& rig = object.rig;
    rig.links.resize(depth);

    pivot_ed = Lerp(pivot_st, pivot_ed, amount);
    rig.pivot = {
        .origin = pivot_st + (pivot_ed - pivot_st) * phase,
        .extent = pivot_ed - pivot_st,
    };

    for (size_t i = 0uz; i < depth; ++i) {
        const auto& st = trace.transforms[i, 1];
        auto ed = trace.transforms[i, 0];

        object.transform = object.transform * Eigen::Translation2f(st.position) * Eigen::Rotation2Df(st.rotation) *
                           Eigen::Scaling(st.scale);

        const Eigen::Vector2f cmp_st = st.scale.cwiseInverse();
        const Eigen::Vector2f cmp_ed = Lerp(cmp_st, ed.scale.cwiseInverse(), amount);
        const Eigen::Vector2f cmp_shift = (cmp_ed - cmp_st) * phase;
        const Eigen::Vector2f cmp_origin = (cmp_st + cmp_shift).cwiseMax(kEpsilon);

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
                    .origin = cmp_origin,
                    .extent = (cmp_ed + cmp_shift).cwiseMax(kEpsilon) - cmp_origin,
                },
            .rotation =
                {
                    .origin = st.rotation + ((ed.rotation - st.rotation) * phase),
                    .extent = ed.rotation - st.rotation,
                },
        };
    }

    object.transform = object.transform * Eigen::Translation2f(-pivot_st);

    return object;
}

[[nodiscard]] MotionMetrics ComputeMotionMetrics(const Object& object, int samples) {
    const auto& rig = object.rig;

    const float step = 1.0f / static_cast<float>(samples);
    const auto base_to_world = object.transform.inverse();
    auto rotors = BuildRotors(object, step);

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

        Eigen::Affine2f smp_to_base = base_to_world;

        for (size_t j = 0uz; j < rig.links.size(); ++j) {
            const auto& link = rig.links[j];
            auto& rotor = rotors[j];

            const Eigen::Vector2f pos = link.position.origin + link.position.extent * t;
            const Eigen::Vector2f scale = (link.compensation.origin + link.compensation.extent * t).cwiseInverse();

            Eigen::Matrix2f linear;
            linear << rotor.origin.cos * scale.x(), -rotor.origin.sin * scale.y(), rotor.origin.sin * scale.x(),
                rotor.origin.cos * scale.y();

            smp_to_base.translation() += smp_to_base.linear() * pos;
            smp_to_base.linear() *= linear;

            rotor.origin = {
                .cos = (rotor.origin.cos * rotor.extent.cos) - (rotor.origin.sin * rotor.extent.sin),
                .sin = (rotor.origin.sin * rotor.extent.cos) + (rotor.origin.cos * rotor.extent.sin),
            };
        }

        const Eigen::Vector2f pivot = rig.pivot.origin + rig.pivot.extent * t;

        const Eigen::Vector2f origin = smp_to_base * -pivot;
        const auto linear = smp_to_base.linear();

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
    auto rotors = BuildRotors(object, step);
    trajectory.resize(samples);

    for (int i = 0; i < samples; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) * step;
        Eigen::Affine2f node = Eigen::Affine2f::Identity();

        for (size_t j = 0uz; j < rig.links.size(); ++j) {
            const auto& link = rig.links[j];
            auto& rotor = rotors[j];

            const Eigen::Vector2f pos = link.position.origin + link.position.extent * t;
            const Eigen::Vector2f cmp = link.compensation.origin + link.compensation.extent * t;

            Eigen::Matrix2f linear;
            linear << cmp.x() * rotor.origin.cos, cmp.x() * rotor.origin.sin, -cmp.y() * rotor.origin.sin,
                cmp.y() * rotor.origin.cos;

            node.translation() = linear * (node.translation() - pos);
            node.linear() = linear * node.linear();

            rotor.origin = {
                .cos = (rotor.origin.cos * rotor.extent.cos) - (rotor.origin.sin * rotor.extent.sin),
                .sin = (rotor.origin.sin * rotor.extent.cos) + (rotor.origin.cos * rotor.extent.sin),
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

    Object object;

    try {
        object = ResolveObject(ctx);
    } catch (const std::exception& e) {
        aul::logger::Error(e.what());
        return false;
    }

    const auto limit = aul::context::GetEditorState() == aul::context::EditorState::kExporting
                           ? static_cast<int>(props::sampling::render::sample_limit.value)
                           : static_cast<int>(props::sampling::viewport::sample_limit.value);

    const auto metrics = ComputeMotionMetrics(object, std::max(limit / 8, 2));
    const int required_samples = static_cast<int>(std::clamp(std::ceil(metrics.length) + 1.0f, 2.0f, 65536.0f));

    const int32_t samples = std::clamp(limit, 2, required_samples);

    {
        Eigen::Vector2f origin;
        Eigen::Vector2f resolution;

        if (props::should_resize.value) {
            const auto size = metrics.box.sizes().array().ceil();

            if ((size > 16384.0f).any()) {
                aul::logger::Warning(L"Image size exceeds maximum limit of 16384x16384");

                resolution = size.cwiseMin(16384.0f);
                origin = metrics.box.min() + ((size.matrix() - resolution) * 0.5f);
            } else {
                resolution = size;
                origin = metrics.box.min();
            }

            Eigen::Map<Eigen::Vector2f>(&ctx->param->cx) -= origin + (resolution - object.dimensions) * 0.5f;
        } else {
            origin = Eigen::Vector2f::Zero();
            resolution = object.dimensions;
        }

        if (!ctx->copy_image_resource(L"resource:image", nullptr)) {
            aul::logger::Error(L"Failed to copy image 'object' to 'resource:image'");
            return false;
        }

        ctx->set_image_data(nullptr, static_cast<int>(resolution.x()), static_cast<int>(resolution.y()));

        {
            static constexpr PIXEL_RGBA clear{0, 0, 0, 0};

            if (props::tint::source::value == 0) {
                const std::wstring_view path{props::tint::image.value};

                if (!path.empty()) {
                    if (!ctx->edit->is_support_media_file(props::tint::image.value, false)) {
                        aul::logger::Error(std::format(L"Unsupported image file format: '{}'", path));
                        return false;
                    }

                    const auto src = std::format(L"image:{}", path);
                    if (!ctx->copy_image_resource(L"resource:map", src.c_str())) {
                        aul::logger::Error(std::format(L"Failed to copy image '{}' to 'resource:map'", src));
                        return false;
                    }
                } else {
                    ctx->create_image_resource(L"resource:map", &clear, 1, 1);
                }
            } else {
                int map_layer = static_cast<int>(props::tint::layer.value);
                if (props::layer_reference::value == 1) {
                    map_layer += ctx->object->effect_layer + 1;
                }

                --map_layer;

                if (map_layer < 0 || map_layer == ctx->object->layer) {
                    ctx->create_image_resource(L"resource:map", &clear, 1, 1);
                } else if (ctx->get_image_object(map_layer, 0.0) == nullptr) {
                    aul::logger::Error(std::format(L"No object exists at layer {}, frame {}", map_layer + 1,
                                                   ctx->object->frame_s + ctx->object->frame));
                    return false;
                } else {
                    const auto src = std::format(L"layer:{}+", map_layer);
                    if (!ctx->copy_image_resource(L"resource:map", src.c_str())) {
                        aul::logger::Error(std::format(L"Failed to copy image '{}' to image 'resource:map'", src));
                        return false;
                    }
                }
            }
        }

        auto* const dst = ctx->get_image_texture2d();
        auto* const img = ctx->get_image_resource_texture2d(L"resource:image");
        auto* const map = ctx->get_image_resource_texture2d(L"resource:map");

        if (img == nullptr || dst == nullptr || map == nullptr) {
            aul::logger::Error(L"Failed to get 'ID3D11Texture2D' pointers");
            return false;
        }

        thread_local std::vector<renderer::Float2x3> trajectory;
        CreateTrajectory(object, samples, trajectory);

        int map_w, _;
        ctx->get_image_resource_size(L"resource:map", &map_w, &_);

        const float mix = std::clamp(static_cast<float>(props::compositing::mix.value) * 0.01f, 0.0f, 1.0f) * 2.0f;
        const float falloff = std::clamp(static_cast<float>(props::shutter::falloff::amount.value) * 0.01f, 0.0f, 1.0f);
        const float edge = std::max(falloff, kEpsilon);

        const renderer::Target target{
            .image = img,
            .map = map,
            .trajectory = trajectory,
        };

        const renderer::Parameter param{
            .transform =
                {

                    {
                        object.transform(0, 0),
                        object.transform(0, 1),
                        object.transform(0, 2),
                    },
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
            .falloff =
                {
                    props::shutter::falloff::edge::value == 0 ? kEpsilon : edge,
                    props::shutter::falloff::edge::value == 1 ? kEpsilon : edge,
                },
            .samples = samples,
            .map_inset = 0.5f / static_cast<float>(map_w),
            .alpha_mode = static_cast<float>(props::compositing::alpha_mode::value),
            .seed = static_cast<float>(resolution.x() * resolution.y()),
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

void Deinit([[maybe_unused]] int64_t id, void* userdata) { delete static_cast<Instance*>(userdata); }

inline constinit auto props = []<size_t... Is>(std::index_sequence<Is...>) {
    return std::to_array<void*>({
        &properties::shutter::name,
        &properties::shutter::angle,
        &properties::shutter::phase,
        &properties::shutter::falloff::name,
        &properties::shutter::falloff::edge::control,
        &properties::shutter::falloff::amount,
        &properties::sampling::name,
        &properties::sampling::viewport::name,
        &properties::sampling::viewport::sample_limit,
        &properties::sampling::render::name,
        &properties::sampling::render::sample_limit,
        &properties::tint::name,
        &properties::tint::source::control,
        &properties::tint::image,
        &properties::tint::layer,
        &properties::tint::visibility::image_selected::image,
        &properties::tint::visibility::layer_selected::layer,
        &properties::compositing::name,
        &properties::compositing::mix,
        &properties::compositing::alpha_mode::control,
        &properties::additional_options,
        &properties::extrapolation::control,
        &properties::layer_reference::control,
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
