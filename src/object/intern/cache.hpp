#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <Eigen/Core>

#pragma warning(push)
#pragma warning(disable : 4201)  // 非標準の無名構造体 (filter2.h FILTER_ITEM_COLOR)
#include <filter2.h>
#pragma warning(pop)

namespace blur::object::cache {
class Store {
  public:
    struct Transform {
        Eigen::Vector2f pivot = Eigen::Vector2f::Zero();
        Eigen::Vector2f position = Eigen::Vector2f::Zero();
        Eigen::Vector2f scale = Eigen::Vector2f::Ones();
        float rotation = 0.0f;
    };

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&) = delete;
    Store& operator=(Store&&) = delete;

    Store() = default;
    ~Store() = default;

    [[nodiscard]] Transform Get(const OBJECT_INFO* ctx, int pos);
    [[nodiscard]] bool Get(const OBJECT_INFO* ctx, std::array<Transform, 2uz>& xforms);
    void Set(const FILTER_PROC_VIDEO* ctx);
    void Set(const OBJECT_INFO* ctx, const std::array<Transform, 2uz>& xforms);

    void Reset();

  private:
    struct Entry {
        Transform transform{};
        std::optional<int> frame = std::nullopt;
    };

    std::mutex mutex_;
    std::unordered_map<int64_t, std::vector<std::array<Entry, 4uz>>> cache_;
};
}  // namespace blur::object::cache
