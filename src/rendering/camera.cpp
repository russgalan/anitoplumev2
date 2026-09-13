#include "camera.hpp"

#include <algorithm>
#include <cmath>

#if ANITOPLUME_V2_HAS_GLM
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#endif

namespace anitoplume
{

Camera::Camera()
    : position_x_(0.0f), position_y_(0.0f), position_z_(0.0f),
      yaw_degrees_(0.0f), pitch_degrees_(0.0f), aspect_ratio_(16.0f / 9.0f),
      field_of_view_degrees_(60.0f), near_plane_(0.1f), far_plane_(100000.0f)
{
}

void Camera::set_aspect_ratio(float aspect_ratio)
{
    if (aspect_ratio > 0.0f) aspect_ratio_ = aspect_ratio;
}

void Camera::set_position(float x, float y, float z)
{
    position_x_ = x;
    position_y_ = y;
    position_z_ = z;
}

void Camera::set_rotation(float yaw_degrees, float pitch_degrees)
{
    yaw_degrees_ = yaw_degrees;
    pitch_degrees_ = std::clamp(pitch_degrees, -89.0f, 89.0f);
}

void Camera::rotate(float yaw_delta_degrees, float pitch_delta_degrees)
{
    yaw_degrees_ += yaw_delta_degrees;
    if (yaw_degrees_ > 360.0f) yaw_degrees_ -= 360.0f;
    if (yaw_degrees_ < -360.0f) yaw_degrees_ += 360.0f;
    pitch_degrees_ = std::clamp(pitch_degrees_ + pitch_delta_degrees, -89.0f, 89.0f);
}

void Camera::zoom(float scroll_steps)
{
    // GLFW reports positive wheel motion for scrolling up. Narrowing the
    // field of view produces a conventional zoom-in effect.
    field_of_view_degrees_ = std::clamp(field_of_view_degrees_ - scroll_steps * 5.0f,
                                        20.0f, 90.0f);
}

void Camera::move_local(float forward, float right, float up)
{
    const float yaw = yaw_degrees_ * 3.14159265359f / 180.0f;
    // With the camera looking along +Y at yaw 90 degrees, screen-right is +X.
    position_x_ += std::cos(yaw) * forward + std::sin(yaw) * right;
    position_y_ += std::sin(yaw) * forward - std::cos(yaw) * right;
    position_z_ += up;
}

#if ANITOPLUME_V2_HAS_GLM

glm::mat4 Camera::view_matrix() const
{
    const glm::vec3 position(position_x_, position_y_, position_z_);
    const glm::vec3 forward(
        std::cos(glm::radians(pitch_degrees_)) * std::cos(glm::radians(yaw_degrees_)),
        std::cos(glm::radians(pitch_degrees_)) * std::sin(glm::radians(yaw_degrees_)),
        std::sin(glm::radians(pitch_degrees_)));
    return glm::lookAt(position, position + forward, glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 Camera::projection_matrix() const
{
    return glm::perspective(glm::radians(field_of_view_degrees_), aspect_ratio_, near_plane_, far_plane_);
}

#else

std::array<float, 16> Camera::view_matrix() const
{
    return {1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -position_x_, -position_y_, -position_z_, 1.0f};
}

std::array<float, 16> Camera::projection_matrix() const
{
    return {1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, -1.0f, -1.0f,
            0.0f, 0.0f, -0.1f, 0.0f};
}

#endif

std::array<float, 16> Camera::view_projection_matrix() const
{
#if ANITOPLUME_V2_HAS_GLM
    glm::mat4 projection = projection_matrix();
    projection[1][1] *= -1.0f;
    const glm::mat4 combined = projection * view_matrix();
    std::array<float, 16> result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            result[static_cast<std::size_t>(column * 4 + row)] = combined[column][row];
    return result;
#else
    const auto projection = projection_matrix();
    const auto view = view_matrix();
    std::array<float, 16> result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result[static_cast<std::size_t>(column * 4 + row)] +=
                    projection[static_cast<std::size_t>(column * 4 + k)] *
                    view[static_cast<std::size_t>(k * 4 + row)];
    return result;
#endif
}

} // namespace anitoplume
