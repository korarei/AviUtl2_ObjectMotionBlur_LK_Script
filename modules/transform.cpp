#include "transform.hpp"

Delta::Delta(const Transform &from, const Transform &to) noexcept :
    rot(to.rotation() - from.rotation()),
    base(from.scale().inverse()),
    scale(base * to.scale()),
    pos(base * (to.position() - from.position()).rotate(-from.rotation())),
    center(from.center() - to.center()),
    flag(is_zero(pos.norm(2)) && is_zero(center.norm(2)) && is_zero(scale.determinant() - 1.0) && is_zero(rot)) {}

Delta::Motion
Delta::build_xform(double amt, int smp, bool inverse) const noexcept {
    if (smp > 0) {
        const double ratio = smp > 1 ? amt / static_cast<double>(smp) : amt;
        const auto t = pos * ratio;
        const auto r = rot * ratio;
        const auto s = scale.pow(ratio);

        if (inverse) {
            const auto pose = base * Mat2<double>::rotation(-r) * base.inverse();
            const auto xform = Mat3<double>(pose, Vec3(-(pose * t), 1.0));
            return {xform, Diag3(s.inverse(), 1.0), Vec3(center * -ratio)};
        } else {
            const auto pose = base * Mat2<double>::rotation(r) * base.inverse();
            const auto xform = Mat3<double>(pose, Vec3(t, 1.0));
            return {xform, Diag3<double>(s, 1.0), Vec3(center * ratio)};
        }
    } else {
        return {Mat3<double>::identity(), Diag3<double>::identity(), Vec3<double>()};
    }
}
