#include "transform_utils.hpp"

// Delta class.
Delta::Delta(const Transform &from, const Transform &to) noexcept :
    rel_rot(to.get_rot() - from.get_rot()),
    rel_scale(std::max(to.get_zoom() / from.get_zoom(), ZOOM_MIN)),
    rel_pos((to.get_pos() - from.get_pos()).rotate(-from.get_rot(), 100.0f / from.get_zoom())),
    rel_center(from.get_center() - to.get_center()),
    center_to(-to.get_center()),
    center_from(-from.get_center()),
    rel_dist(rel_pos.norm(2) + rel_center.norm(2)),
    flag(is_zero(rel_dist) && are_equal(rel_scale, 1.0f) && is_zero(rel_rot)) {}

Mat3<float>
Delta::calc_htm(float amt, std::uint32_t smp, bool is_inv) const noexcept {
    float step_amt = smp > 1 ? amt / static_cast<float>(smp) : amt;
    float rot = rel_rot * step_amt;
    float scale = std::pow(rel_scale, step_amt);
    auto pos = rel_pos * step_amt;

    if (is_inv) {
        auto inv_ori = Mat2<float>::rotation(-rot, 1.0f / scale);
        auto inv_pos = -(inv_ori * pos);
        auto trans = Vec3<float>(inv_pos, 1.0f);
        return Mat3<float>(inv_ori, trans);
    } else {
        auto ori = Mat2<float>::rotation(rot, scale);
        auto trans = Vec3<float>(pos, 1.0f);
        return Mat3<float>(ori, trans);
    }
}
