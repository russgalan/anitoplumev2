#include "plume_simulation.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace anitoplume
{
namespace
{
constexpr float kPi = 3.14159265359f;
constexpr float kGravity = 9.81f;
constexpr float kAirIncorporation = 5.0f;

TerrainVec3 add(TerrainVec3 a, TerrainVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
TerrainVec3 sub(TerrainVec3 a, TerrainVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
TerrainVec3 mul(TerrainVec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(TerrainVec3 a, TerrainVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(TerrainVec3 a) { return std::sqrt(dot(a, a)); }
TerrainVec3 normalize(TerrainVec3 a)
{
    const float len = length(a);
    return len > 1e-6f ? mul(a, 1.0f / len) : TerrainVec3{};
}
TerrainVec3 cross(TerrainVec3 a, TerrainVec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float atmospheric_temperature(float height) { return 288.15f - 6.5f * height / 1000.0f; }
float atmospheric_density(float height)
{
    return 352.995f * std::pow(1.0f - 0.0000225577f * height, 5.25516f) /
           (288.15f - 0.0065f * height);
}
}

PlumeSimulation::PlumeSimulation(TerrainVec3 vent_position, EruptionParameters parameters,
                                 std::uint32_t random_seed)
    : vent_position_(vent_position), parameters_(parameters), random_seed_(random_seed)
{
    wind_profile_ = {{0.0f, {0.0f, 0.0f, 0.0f}}, {2000.0f, {0.0f, 0.0f, 0.0f}},
                     {4000.0f, {0.0f, 0.0f, 0.0f}}, {6000.0f, {0.0f, 0.0f, 0.0f}},
                     {8000.0f, {0.0f, 0.0f, 0.0f}}, {10000.0f, {0.0f, 0.0f, 0.0f}}};
    reset();
}

void PlumeSimulation::reset()
{
    smoke_layers_.clear();
    particles_.clear();
    next_particle_id_ = 0;
    simulation_time_ = 0.0f;
    layer_delay_ = 0.0f;
}

void PlumeSimulation::set_wind_profile(std::vector<WindSample> samples)
{
    if (!samples.empty())
    {
        std::sort(samples.begin(), samples.end(), [](const WindSample& a, const WindSample& b) {
            return a.altitude < b.altitude;
        });
        wind_profile_ = std::move(samples);
    }
}

TerrainVec3 PlumeSimulation::wind_at(float height) const
{
    if (wind_profile_.empty()) return {};
    if (height <= wind_profile_.front().altitude) return wind_profile_.front().velocity;
    for (std::size_t i = 0; i + 1 < wind_profile_.size(); ++i)
    {
        if (height <= wind_profile_[i + 1].altitude)
        {
            const float range = wind_profile_[i + 1].altitude - wind_profile_[i].altitude;
            const float lambda = (height - wind_profile_[i].altitude) / range;
            return add(wind_profile_[i].velocity,
                       mul(sub(wind_profile_[i + 1].velocity, wind_profile_[i].velocity), lambda));
        }
    }
    return wind_profile_.back().velocity;
}

void PlumeSimulation::spawn_layer()
{
    SmokeLayer layer;
    layer.center = vent_position_;
    layer.velocity = {0.0f, 0.0f, static_cast<float>(parameters_.initial_speed)};
    layer.plume_axis = normalize(layer.velocity);
    layer.speed_along_axis = length(layer.velocity);
    layer.radius = static_cast<float>(parameters_.initial_radius);
    layer.thickness = layer.radius;
    layer.density = static_cast<float>(parameters_.initial_density);
    smoke_layers_.push_back(layer);
    add_particles_for_layer(smoke_layers_.size() - 1);
}

void PlumeSimulation::add_particles_for_layer(std::size_t layer_index)
{
    static constexpr unsigned int kParticleCount = 6;
    std::mt19937 generator(random_seed_ + static_cast<std::uint32_t>(layer_index));
    std::uniform_real_distribution<float> angle_offset(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> size(0.5f, 1.5f);
    const float offset = angle_offset(generator);
    float total = 0.0f;
    float sizes[kParticleCount]{};
    for (float& value : sizes) { value = size(generator); total += value; }

    for (unsigned int i = 0; i < kParticleCount; ++i)
    {
        const float angle = offset + static_cast<float>(i) * 2.0f * kPi / kParticleCount;
        PlumeParticle particle;
        particle.id = next_particle_id_++;
        particle.closest_layer = static_cast<std::int32_t>(layer_index);
        particle.radius = static_cast<float>(parameters_.initial_radius) *
                          (sizes[i] * kParticleCount / total);
        particle.position = add(smoke_layers_[layer_index].center,
                                {particle.radius * std::cos(angle), particle.radius * std::sin(angle), 0.0f});
        particle.velocity = smoke_layers_[layer_index].velocity;
        particle.rotation_axis = {-std::sin(angle), std::cos(angle), 0.0f};
        particle.angle_vector = {std::cos(angle), std::sin(angle), 0.0f};
        particle.angular_speed = length(particle.velocity) / particle.radius;
        particle.size_factor = sizes[i] * kParticleCount / total;
        particle.density = smoke_layers_[layer_index].density;
        particles_.push_back(particle);
    }
}

void PlumeSimulation::update_layer(std::size_t index, TerrainVec3 wind, float dt)
{
    SmokeLayer& layer = smoke_layers_[index];
    layer.lifetime += dt;
    const float atm_density = atmospheric_density(layer.center.z);
    float d_mass = 0.0f;

    if (layer.plume && layer.center.z > 0.0f)
    {
        const float volume = kPi * layer.radius * layer.radius * layer.thickness;
        const float diff_density = (layer.stagnates_long ? 0.00005f : 0.00000005f) * dt;
        if (layer.density > diff_density) { layer.density -= diff_density; d_mass -= diff_density * volume; }
    }

    if (layer.rising && !layer.stagnates_long)
    {
        const float thickness = dt * layer.velocity.z;
        const float volume = layer.thickness * kPi * layer.radius * layer.radius;
        const float mass = layer.density * volume;
        const float ks = 0.09f, kw = 0.9f;
        const float theta = layer.theta;
        const float ue = ks * std::fabs(length(layer.velocity) - length(wind) * std::cos(theta)) +
                         kw * std::fabs(length(wind) * std::sin(theta));
        const float r_atm = kAirIncorporation * ue;
        const float atm_volume = thickness * kPi * (2.0f * layer.radius + r_atm) * r_atm;
        const float atm_mass = atm_density * atm_volume;
        const float new_temp = (mass * layer.temperature + atm_mass * atmospheric_temperature(layer.center.z)) /
                               (mass + atm_mass);
        const float new_volume = volume + new_temp * atm_volume / atmospheric_temperature(layer.center.z);
        const float new_density = (mass + atm_mass) / new_volume;
        layer.temperature = new_temp;
        layer.radius = std::cbrt(new_volume / kPi);
        layer.thickness = layer.radius;
        if (layer.density > atm_density && new_density < atm_density) layer.plume = true;
        layer.density = new_density;
        d_mass = atm_mass;
    }

    const float volume = layer.thickness * kPi * layer.radius * layer.radius;
    const float surface = 2.0f * kPi * layer.radius * layer.radius;
    const float surface_effective = 2.0f * layer.radius * layer.radius;
    const float mass = layer.density * volume;
    const TerrainVec3 v_diff = sub(wind, {layer.velocity.x, layer.velocity.y, 0.0f});
    const TerrainVec3 forces = add({0.0f, 0.0f, -mass * kGravity},
        add({0.0f, 0.0f, atm_density * volume * kGravity},
            add(mul(layer.velocity, -0.5f * atm_density * 0.04f * surface * length(layer.velocity)),
                mul(v_diff, 600.0f * length(v_diff) * surface_effective))));
    const float old_mass = mass - d_mass;
    const TerrainVec3 velocity = mul(add(mul(layer.velocity, old_mass), mul(forces, dt)), 1.0f / mass);
    const TerrainVec3 old_velocity = layer.velocity;
    const TerrainVec3 position = add(layer.center, mul(velocity, dt));
    if (old_velocity.z > 0.0f && velocity.z < 0.0f && !layer.plume)
    { layer.rising = false; layer.begin_falling = true; }
    if (layer.plume && !layer.stagnates && layer.center.z > 5000.0f && atm_density - layer.density < 0.01f)
        layer.stagnates = true;
    if (layer.stagnates && velocity.z < 0.0f && !layer.stagnates_long) layer.stagnates_long = true;
    TerrainVec3 new_position = position;
    if (layer.stagnates && new_position.z < layer.center.z) new_position.z = layer.center.z;
    layer.acceleration = mul(velocity, 1.0f / dt);
    layer.velocity = velocity;
    layer.center = {new_position.x, new_position.y, std::max(-1000.0f, new_position.z)};
    layer.speed_along_axis = length(velocity);
    layer.plume_axis = normalize(velocity);
    if (length(wind) != 0.0f && dt > 0.0f)
    {
        layer.theta_axis = normalize(cross(wind, {0.0f, 0.0f, 1.0f}));
        layer.theta = std::asin(std::clamp(velocity.z / std::max(length(velocity), 1e-6f), -1.0f, 1.0f));
        if (layer.theta < 0.0f) layer.theta = 0.0f;
    }
}

void PlumeSimulation::update_particles(float dt)
{
    for (auto& particle : particles_)
    {
        particle.lifetime += dt;
        if (particle.closest_layer < 0 || static_cast<std::size_t>(particle.closest_layer) >= smoke_layers_.size()) continue;
        SmokeLayer& layer = smoke_layers_[static_cast<std::size_t>(particle.closest_layer)];
        if (particle.stagnate_long || particle.falling) continue;
        const TerrainVec3 relative = sub(particle.position, layer.center);
        const float distance = length(relative);
        particle.relative_distance = distance;
        particle.radius = particle.size_factor * layer.radius;
        particle.density = layer.density;
        particle.velocity = mul(layer.plume_axis, layer.speed_along_axis);
        particle.position = add(particle.position, mul(particle.velocity, dt));
        particle.current_angle += (particle.radius > 1e-6f ? layer.speed_along_axis / particle.radius : 0.0f) * dt;
    }
}

void PlumeSimulation::update_stagnating_particles(TerrainVec3 wind, float dt)
{
    for (auto& particle : particles_)
    {
        if (!particle.stagnate_long || particle.closest_layer < 0) continue;
        const SmokeLayer& layer = smoke_layers_[static_cast<std::size_t>(particle.closest_layer)];
        TerrainVec3 radial{particle.position.x, particle.position.y, 0.0f};
        particle.velocity = add(mul(normalize(radial), 50.0f), wind);
        particle.position = add(particle.position, mul(particle.velocity, dt));
        particle.density = layer.density;
    }
}

void PlumeSimulation::advance(float dt, unsigned int substeps)
{
    if (dt <= 0.0f || substeps == 0) return;
    const float step = dt / static_cast<float>(substeps);
    for (unsigned int substep = 0; substep < substeps; ++substep)
    {
        layer_delay_ += step;
        if (smoke_layers_.empty() || layer_delay_ >= parameters_.initial_radius /
                                      (2.0f * parameters_.initial_speed))
        {
            spawn_layer();
            layer_delay_ = 0.0f;
        }
        for (std::size_t i = 0; i < smoke_layers_.size(); ++i)
            update_layer(i, wind_at(smoke_layers_[i].center.z), step);
        update_particles(step);
        update_stagnating_particles(wind_at(0.0f), step);
        simulation_time_ += step;
    }
}

} // namespace anitoplume
