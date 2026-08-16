#pragma once

#include <Eigen/Core>

namespace blur::object {
struct Transform {
    Eigen::Vector2f position = Eigen::Vector2f::Zero();
    Eigen::Vector2f scale = Eigen::Vector2f::Ones();
    float rotation = 0.0f;
};
}  // namespace blur::object
