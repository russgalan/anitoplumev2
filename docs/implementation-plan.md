# Implementation Plan

## Phase 1 — Foundation

- CMake
- C++ project
- GLFW
- Vulkan
- GLM
- VMA
- Dear ImGui

## Phase 2 — Rendering

- Vulkan instance
- Physical device
- Logical device
- Swapchain
- Command buffers
- Synchronization
- Graphics pipeline
- Camera

## Phase 3 — Terrain

- Load Taal mesh
- Load terrain texture
- GPU buffers
- Basic diffuse lighting
- Terrain rendering
- Terrain coordinate system

## Phase 4 — Plume

- Particle structure
- Particle buffer
- Billboard rendering
- Smoke textures
- CPU simulation

## Phase 5 — GPU Simulation

- Compute pipeline
- Particle storage buffer
- Compute dispatch
- Resource barriers
- Compute-to-render synchronization

## Phase 6 — Terrain Interaction

- Terrain height lookup
- Particle-ground collision
- Ground interaction

## Phase 7 — UI

- Simulation parameters
- Camera controls
- Playback
- Plume controls

## Phase 8 — Benchmarking

- FPS
- Frame time
- CPU frame time
- GPU frame time
- Command recording time
- Shader execution time
- GPU memory bandwidth
- CPU utilization

## Phase 9 — Evaluation

- OpenGL baseline
- Vulkan implementation
- Identical hardware
- Identical workload
- RenderDoc
- SUS usability testing