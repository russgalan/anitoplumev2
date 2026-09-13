#include "simulation/plume_simulation.hpp"

#include <cassert>
#include <cmath>

int main()
{
    using namespace anitoplume;
    PlumeSimulation simulation({0.0f, 0.0f, 0.0f}, {150.0, 0.0, 100.0, 200.0, 500.0}, 7);
    simulation.advance(0.1f, 1);
    assert(simulation.smoke_layers().size() == 1);
    assert(simulation.particles().size() == 6);
    const float initial_z = simulation.smoke_layers().front().center.z;
    simulation.advance(1.0f, 10);
    assert(simulation.simulation_time() > 1.0f);
    assert(simulation.smoke_layers().front().center.z > initial_z);
    assert(std::isfinite(simulation.smoke_layers().front().density));
    assert(std::isfinite(simulation.particles().front().position.z));
    return 0;
}
