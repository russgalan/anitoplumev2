#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace anitoplume
{

struct TerrainVec2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct TerrainVec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct TerrainVertex
{
    TerrainVec3 position;
    TerrainVec3 normal;
    TerrainVec2 uv;
};

struct TerrainTransform
{
    // Matches terrain_loader::load_terrain() in V1.
    float rotation_x_radians = 1.57079632679f;
    TerrainVec3 translation{0.0f, 0.0f, -7.0f};
    float scale = 1.0f;
};

struct TerrainAsset
{
    std::string mesh_path;
    std::string diffuse_texture_path;
    std::string normal_texture_path;
    TerrainTransform collision_transform;
    float display_scale = 0.25f;
};

class TerrainMesh
{
public:
    bool load_obj(const std::string& path, std::string& error);
    void apply_transform(const TerrainTransform& transform);

    const std::vector<TerrainVertex>& vertices() const { return vertices_; }
    const std::vector<std::uint32_t>& indices() const { return indices_; }
    bool empty() const { return vertices_.empty() || indices_.empty(); }

private:
    std::vector<TerrainVertex> vertices_;
    std::vector<std::uint32_t> indices_;
};

class TerrainHeightField
{
public:
    // Matches terrain_structure::fill_height_field() in V1.
    static constexpr float kMinCoordinate = -15000.0f;
    static constexpr float kMaxCoordinate = 15000.0f;
    static constexpr float kCellSize = 200.0f;
    static constexpr std::size_t kFieldSize = 150;

    void build(const TerrainMesh& mesh);
    float height_at(float x, float y) const;
    TerrainVec3 normal_at(float x, float y) const;
    bool valid_at(float x, float y) const;

private:
    std::size_t index_for(float coordinate) const;
    std::vector<float> heights_ = std::vector<float>(kFieldSize * kFieldSize, 0.0f);
    std::vector<TerrainVec3> normals_ = std::vector<TerrainVec3>(kFieldSize * kFieldSize, {0.0f, 0.0f, 1.0f});
    std::vector<std::uint8_t> valid_ = std::vector<std::uint8_t>(kFieldSize * kFieldSize, 0);
};

class Terrain
{
public:
    static TerrainAsset default_taal_asset();

    bool validate_asset_files(const TerrainAsset& asset, std::string& error) const;
    bool load(const TerrainAsset& asset, std::string& error);

    const TerrainAsset& asset() const { return asset_; }
    const TerrainMesh& mesh() const { return mesh_; }
    const TerrainHeightField& height_field() const { return height_field_; }
    bool loaded() const { return loaded_; }

private:
    TerrainAsset asset_;
    TerrainMesh mesh_;
    TerrainHeightField height_field_;
    bool loaded_ = false;
};

} // namespace anitoplume
