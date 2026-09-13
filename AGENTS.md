# AnitoPlume V2 Development Instructions

## Project

This repository contains the implementation for the thesis:

"A New Rendering and Plume Simulation Framework
for Visualizing Taal Volcano Eruption Plumes"

## Research Scope

The project modernizes the existing AnitoPlume system.

The following must be preserved:

- Lagrangian particle-based plume simulation model
- Existing simulation behavior
- Taal terrain representation
- Existing user interaction workflow

The research focuses on:

- Explicit graphics API architecture
- GPU resource management
- Command recording
- Pipeline state management
- Shader portability
- GPU synchronization
- Rendering efficiency
- Parallel simulation
- Performance benchmarking

## Primary Technology

- C++
- Vulkan
- GLFW
- GLM
- Vulkan Memory Allocator
- Dear ImGui
- RenderDoc

## Architecture

Keep these systems separate:

- Application
- Scene
- Terrain
- Camera
- Simulation
- Renderer
- GPU resources
- UI
- Profiling

Do not tightly couple simulation logic to Vulkan.

Do not introduce a game engine.

Do not replace the Lagrangian simulation with an Eulerian model.

Do not invent new plume physics unless explicitly requested.

## Development Strategy

Implement incrementally:

1. Project setup
2. Vulkan initialization
3. Swapchain
4. Triangle
5. Camera
6. Terrain
7. Particle rendering
8. CPU simulation
9. GPU compute simulation
10. GPU synchronization
11. Terrain collision
12. UI
13. Profiling
14. Optimization
15. Benchmarking

## Important

Every implementation should remain compatible
with the thesis scope.

Do not prematurely implement:

- ray tracing
- RTXDI
- advanced PBR
- terrain LOD
- tessellation
- advanced terrain effects

unless explicitly requested.

## Validation

After modifying code:

- build the project
- run available tests
- report compilation errors
- do not hide errors
- keep changes incremental