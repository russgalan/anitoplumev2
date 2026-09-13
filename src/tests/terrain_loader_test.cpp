#include "terrain/terrain.hpp"

#include <cmath>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "expected OBJ fixture path\n";
        return 1;
    }

    anitoplume::TerrainMesh mesh;
    std::string error;
    if (!mesh.load_obj(argv[1], error))
    {
        std::cerr << error << '\n';
        return 1;
    }
    if (mesh.vertices().size() != 3 || mesh.indices().size() != 3)
    {
        std::cerr << "unexpected mesh topology\n";
        return 1;
    }

    mesh.apply_transform(anitoplume::TerrainTransform{});
    anitoplume::TerrainHeightField field;
    field.build(mesh);
    if (!field.valid_at(1.0f, 1.0f))
    {
        std::cerr << "transformed vertex was not inserted into the height field\n";
        return 1;
    }
    const auto normal = field.normal_at(1.0f, 1.0f);
    if (std::fabs(normal.y + 1.0f) > 0.001f)
    {
        std::cerr << "V1-compatible X rotation was not applied to the normal\n";
        return 1;
    }
    return 0;
}
