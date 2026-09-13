#include <iostream>
#include <fstream>
#include <optional>
#include <chrono>
#include <algorithm>

#include "anitoplumev2/build_config.hpp"
#include "rendering/camera.hpp"
#include "rendering/vulkan_context.hpp"
#include "terrain/terrain.hpp"
#include "simulation/plume_simulation.hpp"

namespace
{
void print_dependency_status(const char* name, bool available)
{
    std::cout << "  " << name << ": " << (available ? "available" : "not found") << '\n';
}
}

int main(int argc, char** argv)
{
    bool single_frame = false;
    std::string terrain_path;
    std::string vertex_shader_path;
    std::string fragment_shader_path;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--single-frame") single_frame = true;
        else if (argument.rfind("--vertex-shader=", 0) == 0)
            vertex_shader_path = argument.substr(16);
        else if (argument.rfind("--fragment-shader=", 0) == 0)
            fragment_shader_path = argument.substr(18);
        else terrain_path = argv[i];
    }

    std::cout << "AnitoPlumeV2 "
              << ANITOPLUME_V2_VERSION_MAJOR << '.'
              << ANITOPLUME_V2_VERSION_MINOR << '.'
              << ANITOPLUME_V2_VERSION_PATCH << " foundation\n";
    std::cout << "Dependency discovery:\n";
    print_dependency_status("Vulkan", ANITOPLUME_V2_HAS_VULKAN != 0);
    print_dependency_status("GLFW", ANITOPLUME_V2_HAS_GLFW != 0);
    print_dependency_status("GLM", ANITOPLUME_V2_HAS_GLM != 0);
    print_dependency_status("VMA", ANITOPLUME_V2_HAS_VMA != 0);
    print_dependency_status("Dear ImGui", ANITOPLUME_V2_HAS_IMGUI != 0);

    auto terrain_asset = anitoplume::Terrain::default_taal_asset();
    if (!terrain_path.empty()) terrain_asset.mesh_path = terrain_path;
    if (terrain_path.empty())
    {
        anitoplume::Terrain asset_validator;
        std::string asset_error;
        if (!asset_validator.validate_asset_files(terrain_asset, asset_error))
        {
            std::cerr << "Taal terrain asset validation failed: " << asset_error << '\n';
            return 1;
        }
        std::cout << "Taal terrain assets: validated\n";
    }
    std::optional<anitoplume::Terrain> terrain;
    if (std::ifstream(terrain_asset.mesh_path).good())
    {
        terrain.emplace();
        std::string error;
        if (!terrain->load(terrain_asset, error))
        {
            std::cerr << "Terrain loading failed: " << error << '\n';
            return 1;
        }
        std::cout << "Terrain: " << terrain->mesh().vertices().size() << " vertices, "
                  << terrain->mesh().indices().size() / 3 << " triangles\n";
    }
    else
    {
        std::cout << "Terrain mesh not found: " << terrain_asset.mesh_path << '\n';
    }

#if ANITOPLUME_V2_RENDERER_ENABLED
    anitoplume::Camera camera;
    camera.set_aspect_ratio(1280.0f / 720.0f);
    camera.set_position(0.0f, -900.0f, 300.0f);
    camera.set_rotation(90.0f, -18.0f);
    anitoplume::PlumeSimulation plume({0.0f, 0.0f, 0.0f}, {150.0, 0.0, 100.0, 200.0, 500.0});
    auto previous_frame = std::chrono::steady_clock::now();
    anitoplume::VulkanContext renderer;
    if (!renderer.initialize(1280, 720, "AnitoPlumeV2"))
    {
        std::cerr << "Vulkan renderer initialization failed\n";
        return 1;
    }

    if (terrain && !renderer.upload_terrain(terrain->mesh(), terrain->asset().display_scale,
                                            terrain->asset().diffuse_texture_path,
                                            terrain->asset().normal_texture_path))
    {
        std::cerr << "Terrain GPU upload failed\n";
        renderer.shutdown();
        return 1;
    }

    if (!vertex_shader_path.empty() && !fragment_shader_path.empty() &&
        !renderer.create_graphics_pipeline(vertex_shader_path, fragment_shader_path))
    {
        std::cerr << "Terrain graphics pipeline creation failed\n";
        renderer.shutdown();
        return 1;
    }

    do
    {
        renderer.poll_events();
        const auto now = std::chrono::steady_clock::now();
        const float frame_seconds = std::clamp(
            std::chrono::duration<float>(now - previous_frame).count(), 0.001f, 0.1f);
        previous_frame = now;

        constexpr float camera_speed = 1200.0f;
        float forward = 0.0f;
        float right = 0.0f;
        if (renderer.control_key_down(anitoplume::CameraControlKey::Forward)) forward += camera_speed * frame_seconds;
        if (renderer.control_key_down(anitoplume::CameraControlKey::Backward)) forward -= camera_speed * frame_seconds;
        if (renderer.control_key_down(anitoplume::CameraControlKey::Right)) right += camera_speed * frame_seconds;
        if (renderer.control_key_down(anitoplume::CameraControlKey::Left)) right -= camera_speed * frame_seconds;
        camera.move_local(forward, right);
        const float scroll = renderer.consume_scroll_delta();
        camera.zoom(scroll);
        float mouse_delta_x = 0.0f;
        float mouse_delta_y = 0.0f;
        if (renderer.consume_mouse_look_delta(mouse_delta_x, mouse_delta_y))
            camera.rotate(-mouse_delta_x * 0.15f, -mouse_delta_y * 0.15f);

        plume.advance(frame_seconds);
        renderer.draw_frame(camera.view_projection_matrix());
    } while (!single_frame && !renderer.should_close());
    renderer.wait_idle();
    renderer.shutdown();
#else
    std::cout << "Vulkan renderer: unavailable (install Vulkan and GLFW development packages)\n";
#endif

    return 0;
}
