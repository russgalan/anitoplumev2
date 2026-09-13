# AnitoPlumeV2

AnitoPlumeV2 is the Vulkan-based thesis implementation of the AnitoPlume
volcanic plume visualization system. It modernizes the rendering and GPU
resource architecture while preserving the V1 Lagrangian plume model.

The original implementation in `../anitoPlumev1/` is a read-only reference
and is not modified by this project.

## Current status

Completed or available:

- CMake-based C++20 project.
- Vulkan instance, device, swapchain, render pass, synchronization, and
  command buffers.
- VMA-backed terrain and texture resources.
- Taal terrain OBJ loading and asset validation.
- Taal diffuse and normal texture upload and sampling.
- Depth-buffered terrain rendering with basic lighting.
- Camera movement, mouse-look, and mouse-wheel zoom.
- V1-derived CPU smoke-layer and particle simulation foundation.

The current application displays the Taal terrain. Billboard plume rendering,
GPU particle simulation, terrain collision, UI, and benchmarking are still
planned work.

## Requirements

The tested Windows toolchain uses:

- CMake 3.20 or newer.
- C++20 compiler.
- Vulkan SDK or Vulkan development packages.
- GLFW.
- GLM.
- Vulkan Memory Allocator.
- `glslc` for GLSL-to-SPIR-V shader compilation.
- A Vulkan-capable GPU and driver.

The current Windows runtime also requires `glfw3.dll` to be available on
`PATH`, normally from:

```text
C:\msys64\ucrt64\bin
```

Dear ImGui is optional at the current stage and is not required to run the
terrain renderer.

## Configure and build on Windows

Run these commands from the `anitoplumev2` directory using Command Prompt:

```cmd
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

cmake -S . -B build-ucrt-phase3-make -G "MinGW Makefiles" ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DBUILD_TESTING=ON ^
  -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 ^
  -DCMAKE_C_COMPILER=C:/Strawberry/c/bin/gcc.exe ^
  -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe ^
  -DCMAKE_RC_COMPILER=C:/Strawberry/c/bin/windres.exe

cmake --build build-ucrt-phase3-make -- -j1
```

`glslc` automatically compiles the terrain shaders into:

```text
build-ucrt-phase3-make/generated/shaders/
```

## Run the application

Use Command Prompt, not PowerShell syntax:

```cmd
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

.\build-ucrt-phase3-make\anitoplumev2.exe ^
  --vertex-shader=build-ucrt-phase3-make\generated\shaders\terrain.vert.spv ^
  --fragment-shader=build-ucrt-phase3-make\generated\shaders\terrain.frag.spv
```

The application runs continuously until the window is closed. For a single
startup/render validation frame:

```cmd
.\build-ucrt-phase3-make\anitoplumev2.exe ^
  --single-frame ^
  --vertex-shader=build-ucrt-phase3-make\generated\shaders\terrain.vert.spv ^
  --fragment-shader=build-ucrt-phase3-make\generated\shaders\terrain.frag.spv
```

Run from the project directory so the default Taal asset paths resolve:

```text
src/assets/terrains/Taal-Spherical-2_0.obj
src/assets/textures/Taal_Texture_2023.png
src/assets/textures/Taal_Normal_2023.png
```

## Camera controls

- `W` / `S`: move forward and backward.
- `A` / `D`: strafe left and right.
- Hold the right mouse button and move the mouse: rotate the camera.
- Mouse wheel: optical zoom in and out.

## Tests

With the MSYS runtime directory on `PATH`:

```cmd
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
ctest --test-dir build-ucrt-phase3-make --output-on-failure
```

The test suite currently covers foundation startup, terrain loading, and the
CPU plume simulation foundation.

## Project structure

```text
src/
  rendering/    Vulkan context, camera, image loading
  terrain/      Taal mesh loading and terrain data
  simulation/   Vulkan-independent CPU plume simulation
  shaders/      GLSL terrain shaders
  tests/        Automated validation tests
  assets/       Taal terrain and texture assets
docs/           Architecture, V1 analysis, and implementation planning
```

Simulation code is intentionally independent of Vulkan classes. GPU resource
management and rendering remain in the rendering layer.

## Implementation phases

The implementation follows `docs/implementation-plan.md`:

1. Foundation
2. Rendering
3. Terrain
4. Plume
5. GPU simulation
6. Terrain interaction
7. UI
8. Benchmarking
9. Evaluation

The V1 source remains authoritative for existing plume physics, terrain
interaction, particle state, and simulation update behavior.
