#pragma once

#include <array>
#include <optional>
#include <vector>

#include <Eigen/Core>

#include "transform.hpp"

namespace blur::object {
struct Sample {
    Eigen::Vector2f pivot = Eigen::Vector2f::Zero();
    Transform transform{};
    int frame = 0;
};

struct FrameMapping {
    int frame = 0;
    Sample sample{};
};

struct Instance {
    std::vector<std::array<std::optional<FrameMapping>, 4uz>> mappings;
    bool is_restored = false;
};
}  // namespace blur::object
