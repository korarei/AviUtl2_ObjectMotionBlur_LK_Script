#include "cache.hpp"

#include <intern/aviutl/aviutl.hpp>

namespace {
namespace aul = blur::aviutl;

constexpr float kEpsilon = Eigen::NumTraits<float>::dummy_precision();
}  // namespace

namespace blur::object::cache {
Store::Transform Store::Get(const OBJECT_INFO* ctx, int pos) {
    if (ctx->index < 0 || ctx->index >= ctx->num) {
        static const Transform identity{};
        return identity;
    }

    const std::lock_guard lock(mutex_);

    auto& objects = cache_[ctx->effect_id];

    if (objects.size() != static_cast<size_t>(ctx->num)) {
        objects.assign(ctx->num, std::array<Entry, 4uz>{});
    }

    const auto& entries = objects[ctx->index];

    if (pos < -1 || pos > static_cast<int>(entries.size() - 2uz)) {
        pos = 0;
    }

    if (const auto& entry = entries[static_cast<size_t>(pos) + 1uz]; entry.frame.has_value()) {
        return entry.transform;
    }

    return entries[1uz].transform;
}

bool Store::Get(const OBJECT_INFO* ctx, std::array<Transform, 2uz>& xforms) {
    if (ctx->index < 0 || ctx->index >= ctx->num) {
        return false;
    }

    const std::lock_guard lock(mutex_);

    auto& objects = cache_[ctx->effect_id];

    if (objects.size() != static_cast<size_t>(ctx->num)) {
        objects.assign(ctx->num, std::array<Entry, 4uz>{});
    }

    const auto& entries = objects[ctx->index];

    for (size_t i = 2uz; i < entries.size(); ++i) {
        if (!entries[i].frame.has_value()) {
            return false;
        }
    }

    for (size_t i = 0uz; i < xforms.size(); ++i) {
        xforms[i] = entries[i + 2uz].transform;
    }

    return true;
}

void Store::Set(const FILTER_PROC_VIDEO* ctx) {
    if (ctx->object->index < 0 || ctx->object->index >= ctx->object->num) {
        return;
    }

    const std::lock_guard lock(mutex_);

    auto& objects = cache_[ctx->object->effect_id];

    if (objects.size() != static_cast<size_t>(ctx->object->num)) {
        objects.assign(ctx->object->num, std::array<Entry, 4uz>{});
    }

    auto& entries = objects[ctx->object->index];

    auto& prev = entries[0uz];
    auto& curr = entries[1uz];

    if (!curr.frame.has_value() || *curr.frame != ctx->object->frame) {
        prev = curr;
    }

    curr = {
        .transform =
            {
                .pivot = Eigen::Map<const Eigen::Vector2f>(&ctx->param->cx),
                .position = Eigen::Map<const Eigen::Vector2f>(&ctx->param->x),
                .scale = Eigen::Map<const Eigen::Vector2f>(&ctx->param->sx),
                .rotation = ctx->param->rz,
            },
        .frame = ctx->object->frame,
    };

    if (ctx->object->frame > 0 && ctx->object->frame < static_cast<int>(entries.size() - 1uz)) {
        entries[static_cast<size_t>(ctx->object->frame) + 1uz] = curr;
    }

    if (ctx->object->frame != 0) {
        if (prev.frame.has_value()) {
            if (const int d = ctx->object->frame - *prev.frame; d != 0 && d != 1) {
                const float t = 1.0f / static_cast<float>(d);

                const auto lerp = [t](const Eigen::Vector2f& a, const Eigen::Vector2f& b) -> Eigen::Vector2f {
                    return a + (b - a) * t;
                };

                prev.transform.pivot = lerp(curr.transform.pivot, prev.transform.pivot);
                prev.transform.position = lerp(curr.transform.position, prev.transform.position);
                prev.transform.scale = lerp(curr.transform.scale, prev.transform.scale).cwiseMax(kEpsilon);
                prev.transform.rotation = std::lerp(curr.transform.rotation, prev.transform.rotation, t);

                if (d < 0) {
                    aul::Logger::Warning(L"Reverse playback is not supported");
                }
            }
        }
    } else {
        prev.frame = std::nullopt;
    }
}

void Store::Set(const OBJECT_INFO* ctx, const std::array<Transform, 2uz>& xforms) {
    if (ctx->index < 0 || ctx->index >= ctx->num) {
        return;
    }

    const std::lock_guard lock(mutex_);

    auto& objects = cache_[ctx->effect_id];

    if (objects.size() != static_cast<size_t>(ctx->num)) {
        objects.assign(ctx->num, std::array<Entry, 4uz>{});
    }

    auto& entries = objects[ctx->index];

    for (size_t i = 2uz; i < entries.size(); ++i) {
        if (!entries[i].frame.has_value()) {
            entries[i] = {.transform = xforms[i - 2uz], .frame = static_cast<int>(i) - 1};
        }
    }
}

void Store::Reset() {
    const std::lock_guard lock(mutex_);

    std::unordered_map<int64_t, std::vector<std::array<Entry, 4uz>>>{}.swap(cache_);
}
}  // namespace blur::object::cache
