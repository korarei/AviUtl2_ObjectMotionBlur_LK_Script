#include "cache.hpp"

namespace {
constexpr float kEpsilon = Eigen::NumTraits<float>::dummy_precision();
}

namespace blur::object::cache {
const Store::Transform& Store::Get(const OBJECT_INFO* ctx, int pos) {
    if (ctx->index < 0 || ctx->index >= ctx->num) {
        static const Transform identity{};
        return identity;
    }

    auto& objects = cache_[ctx->effect_id];

    if (objects == nullptr) {
        objects = std::make_shared<std::vector<std::array<Entry, 6uz>>>();
    }

    if (objects->size() != static_cast<size_t>(ctx->num)) {
        objects->assign(ctx->num, std::array<Entry, 6uz>{});
    }

    const auto& entries = (*objects)[ctx->index];

    if (pos < -1 || pos > 2) {
        pos = 0;
    }

    if (const auto& entry = entries[static_cast<size_t>(pos) + 1uz]; entry.frame.has_value()) {
        return entry.transform;
    }

    return entries[1uz].transform;
}

void Store::Set(const FILTER_PROC_VIDEO* ctx) {
    if (ctx->object->index < 0 || ctx->object->index >= ctx->object->num) {
        return;
    }

    auto& objects = cache_[ctx->object->effect_id];

    if (objects == nullptr) {
        objects = std::make_shared<std::vector<std::array<Entry, 6uz>>>();
    }

    if (objects->size() != static_cast<size_t>(ctx->object->num)) {
        objects->assign(ctx->object->num, std::array<Entry, 6uz>{});
    }

    auto& entries = (*objects)[ctx->object->index];

    int d = 0;

    {
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

        if (ctx->object->frame > 0 && ctx->object->frame < 5) {
            entries[static_cast<size_t>(ctx->object->frame) + 1uz] = curr;
        }

        if (prev.frame.has_value()) {
            d = ctx->object->frame - *prev.frame;
        }
    }

    if (d != 0 && d != 1) {
        const float scale = 1.0f / static_cast<float>(d);

        const auto& curr = entries[1uz].transform;
        auto& prev = entries[0uz].transform;

        prev.pivot = (prev.pivot - curr.pivot) * scale + curr.pivot;
        prev.position = (prev.position - curr.position) * scale + curr.position;
        prev.scale = ((prev.scale - curr.scale) * scale + curr.scale).cwiseMax(kEpsilon);
        prev.rotation = ((prev.rotation - curr.rotation) * scale) + curr.rotation;
    }
}

void Store::Reset() {
    std::unordered_map<int64_t, std::shared_ptr<std::vector<std::array<Entry, 6uz>>>>{}.swap(cache_);
}
}  // namespace blur::object::cache
