# software_renderer
Yet another implementation of software 3d rendering 

## Description
A C++17 CMake project for software-based 3D rendering using SDL3 for window management and GLM for mathematics.

## Dependencies
This project uses git submodules for third-party libraries:
- **SDL3** (release-3.2.28): Simple DirectMedia Layer for window creation and event handling
- **GLM** (1.0.2): OpenGL Mathematics library for vector and matrix operations

Optional dependency:
- **oneTBB**: pulled via CMake `FetchContent` when `SWR_ENABLE_TBB=ON` for future parallel tiled rendering

## Prerequisites
- CMake 3.20 or higher
- C++17 compatible compiler (GCC, Clang, or MSVC)
- Git
- X11 development libraries (on Linux): `libx11-dev libxext-dev`

## Building

### Clone the repository with submodules
```bash
git clone --recursive https://github.com/ShouldBeLightWay/software_renderer.git
cd software_renderer
```

If you already cloned without `--recursive`, initialize submodules:
```bash
git submodule update --init --recursive
```

### Build instructions
```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

To prepare oneTBB through CMake for the future parallel tiled path:
```bash
cmake -S . -B build -DSWR_ENABLE_TBB=ON
cmake --build build
```

The executable `software_renderer` will be created in the build directory.

## Running
```bash
./build/software_renderer
```

This will open an 800x600 window. Close the window or press the window close button to exit.

Global runtime controls:
- `T` toggle tiled renderer on/off
- `-` halve tile size
- `+` double tile size

## Project Structure
```
software_renderer/
├── CMakeLists.txt       # Root CMake configuration
├── src/                 # Source files
│   └── main.cpp        # Main application entry point
├── 3rdparty/           # Third-party libraries (git submodules)
│   ├── SDL/            # SDL3 library
│   └── glm/            # GLM library
└── build/              # Build directory (generated)
```
