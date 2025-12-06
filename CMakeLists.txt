cmake_minimum_required(VERSION 3.20)
project(software_renderer CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add subdirectories for 3rd party libraries
# Configure SDL3 options before adding subdirectory
set(SDL_VIDEO_DRIVER_DUMMY ON CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN ON CACHE BOOL "" FORCE)
set(SDL_VIDEO_ALLOW_APPLE_BUILD_WITHOUT_DRIVER ON CACHE BOOL "" FORCE)

add_subdirectory(3rdparty/SDL)
add_subdirectory(3rdparty/glm)

# Create executable
add_executable(software_renderer src/main.cpp)

# Link libraries
target_link_libraries(software_renderer PRIVATE SDL3::SDL3)
target_link_libraries(software_renderer PRIVATE glm::glm)

# Include directories
target_include_directories(software_renderer PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/glm
)
