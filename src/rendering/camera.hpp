#pragma once

#include <array>

#include "anitoplumev2/build_config.hpp"

#if ANITOPLUME_V2_HAS_GLM
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#endif

namespace anitoplume
{

class Camera
{
public:
    Camera();

    void set_aspect_ratio(float aspect_ratio);
    void set_position(float x, float y, float z);
    void set_rotation(float yaw_degrees, float pitch_degrees);
    void rotate(float yaw_delta_degrees, float pitch_delta_degrees);
    void zoom(float scroll_steps);
    void move_local(float forward, float right, float up = 0.0f);

    float aspect_ratio() const { return aspect_ratio_; }
    float field_of_view_degrees() const { return field_of_view_degrees_; }

#if ANITOPLUME_V2_HAS_GLM
    glm::mat4 view_matrix() const;
    glm::mat4 projection_matrix() const;
#else
    std::array<float, 16> view_matrix() const;
    std::array<float, 16> projection_matrix() const;
#endif

    std::array<float, 16> view_projection_matrix() const;

private:
    float position_x_;
    float position_y_;
    float position_z_;
    float yaw_degrees_;
    float pitch_degrees_;
    float aspect_ratio_;
    float field_of_view_degrees_;
    float near_plane_;
    float far_plane_;
};

} // namespace anitoplume
