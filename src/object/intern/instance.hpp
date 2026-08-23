#pragma once

#include <array>
#include <vector>

#include <Eigen/Core>

#include "transform.hpp"

namespace blur::object {
struct Sample {
    Eigen::Vector2f pivot = Eigen::Vector2f::Zero();
    Transform transform{};
    int frame = -1;
};

struct FrameMapping {
    int frame = -1;
    Sample sample{};
};

struct State {
    std::array<FrameMapping, 2uz> history{};
    std::vector<Sample> samples;
};

struct Instance {
    bool is_restored = false;
    std::vector<State> states;
};
}  // namespace blur::object
