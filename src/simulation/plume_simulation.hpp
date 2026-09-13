#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "terrain/terrain.hpp"

namespace anitoplume
{

struct EruptionParameters
{
    double initial_speed = 150.0;
    double initial_altitude = 0.0;
    double initial_radius = 100.0;
    double initial_density = 200.0;
    double max_radius = 500.0;
};

struct WindSample
{
    float altitude = 0.0f;
    TerrainVec3 velocity{};
};

struct SmokeLayer
{
    TerrainVec3 center{};
    TerrainVec3 velocity{0.0f, 0.0f, 200.0f};
    TerrainVec3 acceleration{};
    TerrainVec3 plume_axis{0.0f, 0.0f, 1.0f};
    float speed_along_axis = 200.0f;
    float theta = 1.57079632679f;
    TerrainVec3 theta_axis{1.0f, 0.0f, 0.0f};
    float radius = 100.0f;
    float temperature = 1273.0f;
    float density = 1.5f;
    float thickness = 100.0f;
    float lifetime = 0.0f;
    bool rising = true;
    bool begin_falling = false;
    bool falling = false;
    bool plume = false;
    bool stagnates = false;
    bool stagnates_long = false;
    bool secondary_plume = false;
};

// CPU particle representation corresponding to V1 free_sphere_params.
struct PlumeParticle
{
    std::uint32_t id = 0;
    TerrainVec3 position{};
    TerrainVec3 velocity{};
    TerrainVec3 rotation_axis{1.0f, 0.0f, 0.0f};
    TerrainVec3 angle_vector{0.0f, 1.0f, 0.0f};
    float radius = 0.0f;
    float density = 0.0f;
    float lifetime = 0.0f;
    float angular_speed = 0.0f;
    float current_angle = 0.0f;
    float perturbation = 0.0f;
    float size_factor = 1.0f;
    float relative_distance = 0.0f;
    std::int32_t closest_layer = -1;
    float stagnation_altitude = 0.0f;
    bool stagnate = false;
    bool stagnate_long = false;
    bool falling = false;
    bool secondary_column = false;
};

class PlumeSimulation
{
public:
    PlumeSimulation(TerrainVec3 vent_position, EruptionParameters parameters,
                    std::uint32_t random_seed = 1);

    void reset();
    void set_wind_profile(std::vector<WindSample> samples);
    void advance(float dt, unsigned int substeps = 10);

    const std::vector<SmokeLayer>& smoke_layers() const { return smoke_layers_; }
    const std::vector<PlumeParticle>& particles() const { return particles_; }
    float simulation_time() const { return simulation_time_; }

private:
    TerrainVec3 wind_at(float height) const;
    void spawn_layer();
    void update_layer(std::size_t index, TerrainVec3 wind, float dt);
    void update_particles(float dt);
    void update_stagnating_particles(TerrainVec3 wind, float dt);
    void add_particles_for_layer(std::size_t layer_index);

    TerrainVec3 vent_position_;
    EruptionParameters parameters_;
    std::vector<WindSample> wind_profile_;
    std::vector<SmokeLayer> smoke_layers_;
    std::vector<PlumeParticle> particles_;
    std::uint32_t next_particle_id_ = 0;
    std::uint32_t random_seed_ = 1;
    float simulation_time_ = 0.0f;
    float layer_delay_ = 0.0f;
};

} // namespace anitoplume
