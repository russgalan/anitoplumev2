#include "terrain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace anitoplume
{
namespace
{
TerrainVec3 add(TerrainVec3 a, TerrainVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
TerrainVec3 subtract(TerrainVec3 a, TerrainVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
TerrainVec3 multiply(TerrainVec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
TerrainVec3 cross(TerrainVec3 a, TerrainVec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(TerrainVec3 a, TerrainVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
TerrainVec3 normalize(TerrainVec3 value)
{
    const float length = std::sqrt(dot(value, value));
    if (length <= std::numeric_limits<float>::epsilon()) return {0.0f, 0.0f, 1.0f};
    return multiply(value, 1.0f / length);
}

struct FaceIndex
{
    int position = 0;
    int uv = 0;
    int normal = 0;
    bool operator==(const FaceIndex& other) const
    {
        return position == other.position && uv == other.uv && normal == other.normal;
    }
};

struct FaceIndexHash
{
    std::size_t operator()(const FaceIndex& value) const
    {
        std::size_t result = static_cast<std::size_t>(value.position);
        result = result * 31U + static_cast<std::size_t>(value.uv);
        result = result * 31U + static_cast<std::size_t>(value.normal);
        return result;
    }
};

int resolve_index(int index, std::size_t count)
{
    if (index > 0) return index - 1;
    if (index < 0) return static_cast<int>(count) + index;
    return -1;
}

bool parse_face_index(const std::string& token, FaceIndex& result)
{
    std::stringstream stream(token);
    std::string value;
    if (!std::getline(stream, value, '/')) return false;
    result.position = std::stoi(value);
    if (std::getline(stream, value, '/'))
    {
        if (!value.empty()) result.uv = std::stoi(value);
        if (std::getline(stream, value, '/') && !value.empty()) result.normal = std::stoi(value);
    }
    return true;
}

TerrainVec3 transform_position(TerrainVec3 position, const TerrainTransform& transform)
{
    const float c = std::cos(transform.rotation_x_radians);
    const float s = std::sin(transform.rotation_x_radians);
    const TerrainVec3 rotated{position.x, c * position.y - s * position.z,
                              s * position.y + c * position.z};
    return add(multiply(rotated, transform.scale), transform.translation);
}

TerrainVec3 transform_normal(TerrainVec3 normal, const TerrainTransform& transform)
{
    const float c = std::cos(transform.rotation_x_radians);
    const float s = std::sin(transform.rotation_x_radians);
    return normalize({normal.x, c * normal.y - s * normal.z, s * normal.y + c * normal.z});
}
} // namespace

bool TerrainMesh::load_obj(const std::string& path, std::string& error)
{
    vertices_.clear();
    indices_.clear();
    std::ifstream file(path);
    if (!file)
    {
        error = "Unable to open terrain OBJ: " + path;
        return false;
    }

    std::vector<TerrainVec3> positions;
    std::vector<TerrainVec3> normals;
    std::vector<TerrainVec2> uvs;
    std::unordered_map<FaceIndex, std::uint32_t, FaceIndexHash> vertex_map;
    std::vector<TerrainVec3> generated_normal(vertices_.size(), {0.0f, 0.0f, 0.0f});

    std::string line;
    while (std::getline(file, line))
    {
        std::stringstream stream(line);
        std::string type;
        stream >> type;
        if (type == "v")
        {
            TerrainVec3 value;
            stream >> value.x >> value.y >> value.z;
            positions.push_back(value);
        }
        else if (type == "vt")
        {
            TerrainVec2 value;
            stream >> value.x >> value.y;
            uvs.push_back(value);
        }
        else if (type == "vn")
        {
            TerrainVec3 value;
            stream >> value.x >> value.y >> value.z;
            normals.push_back(value);
        }
        else if (type == "f")
        {
            std::vector<FaceIndex> face;
            std::string token;
            while (stream >> token)
            {
                FaceIndex index;
                if (!parse_face_index(token, index))
                {
                    error = "Malformed face in terrain OBJ: " + path;
                    return false;
                }
                face.push_back(index);
            }
            if (face.size() < 3) continue;

            auto make_vertex = [&](const FaceIndex& input) -> std::uint32_t {
                FaceIndex resolved = input;
                resolved.position = resolve_index(input.position, positions.size());
                resolved.uv = input.uv == 0 ? -1 : resolve_index(input.uv, uvs.size());
                resolved.normal = input.normal == 0 ? -1 : resolve_index(input.normal, normals.size());
                const auto found = vertex_map.find(resolved);
                if (found != vertex_map.end()) return found->second;
                if (resolved.position < 0 || static_cast<std::size_t>(resolved.position) >= positions.size())
                    return std::numeric_limits<std::uint32_t>::max();

                TerrainVertex vertex;
                vertex.position = positions[resolved.position];
                if (resolved.uv >= 0 && static_cast<std::size_t>(resolved.uv) < uvs.size()) vertex.uv = uvs[resolved.uv];
                if (resolved.normal >= 0 && static_cast<std::size_t>(resolved.normal) < normals.size()) vertex.normal = normals[resolved.normal];
                const auto output = static_cast<std::uint32_t>(vertices_.size());
                vertices_.push_back(vertex);
                generated_normal.push_back({0.0f, 0.0f, 0.0f});
                vertex_map.emplace(resolved, output);
                return output;
            };

            for (std::size_t i = 1; i + 1 < face.size(); ++i)
            {
                const std::uint32_t a = make_vertex(face[0]);
                const std::uint32_t b = make_vertex(face[i]);
                const std::uint32_t c = make_vertex(face[i + 1]);
                if (a == std::numeric_limits<std::uint32_t>::max() ||
                    b == std::numeric_limits<std::uint32_t>::max() ||
                    c == std::numeric_limits<std::uint32_t>::max())
                {
                    error = "Face references a missing vertex in terrain OBJ: " + path;
                    return false;
                }
                indices_.insert(indices_.end(), {a, b, c});
                const TerrainVec3 face_normal = cross(subtract(vertices_[b].position, vertices_[a].position),
                                                       subtract(vertices_[c].position, vertices_[a].position));
                generated_normal[a] = add(generated_normal[a], face_normal);
                generated_normal[b] = add(generated_normal[b], face_normal);
                generated_normal[c] = add(generated_normal[c], face_normal);
            }
        }
    }

    for (std::size_t i = 0; i < vertices_.size(); ++i)
    {
        if (dot(vertices_[i].normal, vertices_[i].normal) <= std::numeric_limits<float>::epsilon())
            vertices_[i].normal = normalize(generated_normal[i]);
        else vertices_[i].normal = normalize(vertices_[i].normal);
    }
    if (vertices_.empty() || indices_.empty())
    {
        error = "Terrain OBJ contains no drawable triangles: " + path;
        return false;
    }
    return true;
}

void TerrainMesh::apply_transform(const TerrainTransform& transform)
{
    for (auto& vertex : vertices_)
    {
        vertex.position = transform_position(vertex.position, transform);
        vertex.normal = transform_normal(vertex.normal, transform);
    }
}

std::size_t TerrainHeightField::index_for(float coordinate) const
{
    const float normalized = (coordinate - kMinCoordinate) / (kMaxCoordinate - kMinCoordinate);
    const auto index = static_cast<std::size_t>(normalized * static_cast<float>(kFieldSize));
    return std::min(index, kFieldSize - 1);
}

void TerrainHeightField::build(const TerrainMesh& mesh)
{
    std::fill(heights_.begin(), heights_.end(), 0.0f);
    std::fill(normals_.begin(), normals_.end(), TerrainVec3{0.0f, 0.0f, 1.0f});
    std::fill(valid_.begin(), valid_.end(), 0);
    for (const auto& vertex : mesh.vertices())
    {
        if (vertex.position.x <= kMinCoordinate || vertex.position.x >= kMaxCoordinate ||
            vertex.position.y <= kMinCoordinate || vertex.position.y >= kMaxCoordinate) continue;
        const std::size_t x = index_for(vertex.position.x);
        const std::size_t y = index_for(vertex.position.y);
        const std::size_t index = y * kFieldSize + x;
        heights_[index] = vertex.position.z;
        normals_[index] = vertex.normal;
        valid_[index] = 1;
    }
}

float TerrainHeightField::height_at(float x, float y) const
{
    return heights_[index_for(x) + index_for(y) * kFieldSize];
}

TerrainVec3 TerrainHeightField::normal_at(float x, float y) const
{
    return normals_[index_for(x) + index_for(y) * kFieldSize];
}

bool TerrainHeightField::valid_at(float x, float y) const
{
    return valid_[index_for(x) + index_for(y) * kFieldSize] != 0;
}

TerrainAsset Terrain::default_taal_asset()
{
    return {"src/assets/terrains/Taal-Spherical-2_0.obj",
            "src/assets/textures/Taal_Texture_2023.png",
            "src/assets/textures/Taal_Normal_2023.png",
            TerrainTransform{},
            0.25f};
}

bool Terrain::validate_asset_files(const TerrainAsset& asset, std::string& error) const
{
    const std::array<unsigned char, 8> png_signature{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    for (const auto& path : {asset.mesh_path, asset.diffuse_texture_path, asset.normal_texture_path})
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "Missing terrain asset: " + path;
            return false;
        }
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".png")
        {
            std::array<unsigned char, 8> signature{};
            file.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
            if (file.gcount() != static_cast<std::streamsize>(signature.size()) || signature != png_signature)
            {
                error = "Invalid PNG terrain texture: " + path;
                return false;
            }
        }
    }
    return true;
}

bool Terrain::load(const TerrainAsset& asset, std::string& error)
{
    asset_ = asset;
    loaded_ = false;
    if (!mesh_.load_obj(asset.mesh_path, error)) return false;
    mesh_.apply_transform(asset.collision_transform);
    height_field_.build(mesh_);
    loaded_ = true;
    return true;
}

} // namespace anitoplume
