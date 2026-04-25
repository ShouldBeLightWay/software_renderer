cmake_minimum_required(VERSION 3.20)
project(software_renderer CXX)

include(FetchContent)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SWR_ENABLE_TBB "Enable oneTBB dependency for future parallel tiled rendering" OFF)


set( TBB_TEST_ENABLE OFF CACHE BOOL "" FORCE )
set( TBB_EXAMPLES_ENABLE OFF CACHE BOOL "" FORCE )
set( TBB_BENCH_ENABLE OFF CACHE BOOL "" FORCE )
if(SWR_ENABLE_TBB)
	FetchContent_Declare(
		oneTBB
		GIT_REPOSITORY https://github.com/uxlfoundation/oneTBB.git
		GIT_TAG v2022.1.0
		GIT_SHALLOW TRUE
	)
	FetchContent_MakeAvailable(oneTBB)
endif()

# Add subdirectories for 3rd party libraries
# Configure SDL3 options before adding subdirectory
#set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)

add_subdirectory( 3rdparty )

# Create executable
#add_executable(software_renderer src/main.cpp)

# Link libraries
#target_link_libraries(software_renderer PRIVATE SDL3::SDL3 glm::glm)
add_subdirectory( src )
