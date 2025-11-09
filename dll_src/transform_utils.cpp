#include "transform_utils.hpp"

// Delta class.
Delta::Delta(const Transform &from, const Transform &to) noexcept :
    rel_rot(to.get_rot() - from.get_rot()),
    rel_scale(std::max(to.get_zoom() / from.get_zoom(), ZOOM_MIN)),
    rel_pos((to.get_pos() - from.get_pos()).rotate(-from.get_rot(), 100.0 / from.get_zoom())),
    rel_center(from.get_center() - to.get_center()),
    flag(is_zero(rel_pos.norm(2)) && is_zero(rel_center.norm(2)) && are_equal(rel_scale, 1.0f) && is_zero(rel_rot)) {}

Mat3<double>
Delta::calc_htm(double amt, int smp, bool is_inv) const noexcept {
    double step_amt = smp > 1 ? amt / static_cast<double>(smp) : amt;
    double rot = rel_rot * step_amt;
    double scale = std::pow(rel_scale, step_amt);
    auto pos = rel_pos * step_amt;

    if (is_inv) {
        auto inv_ori = Mat2<double>::rotation(-rot, 1.0 / scale);
        auto inv_pos = -(inv_ori * pos);
        auto trans = Vec3<double>(inv_pos, 1.0);
        return Mat3<double>(inv_ori, trans);
    } else {
        auto ori = Mat2<double>::rotation(rot, scale);
        auto trans = Vec3<double>(pos, 1.0);
        return Mat3<double>(ori, trans);
    }
}
