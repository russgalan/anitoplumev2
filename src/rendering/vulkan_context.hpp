#pragma once

#include <cstdint>
#include <string>

namespace anitoplume
{

enum class CameraControlKey
{
    Forward,
    Backward,
    Left,
    Right
};

class TerrainMesh;

class VulkanContext
{
public:
    VulkanContext() = default;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    ~VulkanContext();

    bool initialize(std::uint32_t width, std::uint32_t height, const char* title);
    bool should_close() const;
    void poll_events() const;
    bool consume_mouse_look_delta(float& delta_x, float& delta_y);
    float consume_scroll_delta();
    bool control_key_down(CameraControlKey key) const;
    void draw_frame();
    void draw_frame(const std::array<float, 16>& view_projection);
    bool upload_terrain(const TerrainMesh& terrain, float display_scale = 1.0f,
                        const std::string& diffuse_path = {}, const std::string& normal_path = {});
    void wait_idle();
    void shutdown();

    // Creates the Phase 2 graphics-pipeline objects from SPIR-V modules.
    // The pipeline is optional for the clear-only bootstrap render pass.
    bool create_graphics_pipeline(const std::string& vertex_shader_path,
                                  const std::string& fragment_shader_path);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace anitoplume
