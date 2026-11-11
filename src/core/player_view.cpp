// Markers step 3 (player_view.h).
#include "core/player_view.h"

#include <cmath>

namespace emf::core {

namespace {

float degrees_of(double fx, double fz) {
    if (!std::isfinite(fx) || !std::isfinite(fz) || (std::fabs(fx) < 1e-9 && std::fabs(fz) < 1e-9)) return 0.0f;
    double d = std::atan2(fx, fz) * 57.29577951308232;
    if (d < 0) d += 360.0;
    if (d >= 360.0) d -= 360.0;
    return static_cast<float>(d);
}

}  // namespace

// forward = q * (0, 0, 1)
float heading_from_quat(float x, float y, float z, float w) {
    const double fx = 2.0 * (static_cast<double>(x) * z + static_cast<double>(w) * y);
    const double fz = 1.0 - 2.0 * (static_cast<double>(x) * x + static_cast<double>(y) * y);
    if (x == 0 && y == 0 && z == 0 && w == 0) return 0.0f;
    return degrees_of(fx, fz);
}

// the camera's forward in the view matrix's columns (x v[2], z v[10]; markers.test puts a marker ahead with it)
float yaw_from_view(const float v[16]) { return v ? degrees_of(v[2], v[10]) : 0.0f; }

}  // namespace emf::core
