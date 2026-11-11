#pragma once
// Markers step 3 (API v35): the local player's heading from their rotation and the camera's yaw from its view matrix,
// in map degrees (0 north = +z, 90 east = +x, 0..360). Game-free.
namespace emf::core {

float heading_from_quat(float x, float y, float z, float w);
float yaw_from_view(const float v[16]);

}  // namespace emf::core
