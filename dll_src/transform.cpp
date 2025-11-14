#include "transform.hpp"

// Delta class.
Delta::Delta(const Transform &from, const Transform &to) noexcept :
    rot(to.rotation() - from.rotation()),
    scale(to.scale() / from.scale()),
    pos((to.position() - from.position()).rotate(-from.rotation(), 1.0 / from.scale())),
    center(from.center() - to.center()),
    flag(is_zero(pos.norm(2)) && is_zero(center.norm(2)) && is_zero(scale - 1.0) && is_zero(rot)) {}

Delta::Motion
Delta::compute_motion(double amt, int smp, bool invert) const noexcept {
    const double step_amt = smp > 1 ? amt / static_cast<double>(smp) : amt;
    const double step_rot = rot * step_amt;
    const double step_scale = std::pow(scale, step_amt);
    const auto step_pos = pos * step_amt;

    if (invert) {
        auto pose = Mat2<double>::rotation(-step_rot, 1.0 / step_scale);
        auto trans = Vec3<double>(-(pose * step_pos), 1.0);
        return Motion(Mat3<double>(pose, trans), -center * step_amt);
    } else {
        auto pose = Mat2<double>::rotation(step_rot, step_scale);
        auto trans = Vec3<double>(step_pos, 1.0);
        return Motion(Mat3<double>(pose, trans), center * step_amt);
    }
}
