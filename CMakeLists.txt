cmake_minimum_required(VERSION 3.20)
project(software_renderer CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add subdirectories for 3rd party libraries
# Configure SDL3 options before adding subdirectory
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)

add_subdirectory(3rdparty/SDL)
add_subdirectory(3rdparty/glm)

# Create executable
add_executable(software_renderer src/main.cpp)

# Link libraries
target_link_libraries(software_renderer PRIVATE SDL3::SDL3 glm::glm)

	# On Windows, ensure SDL3 runtime DLL is copied next to the executable
	if (WIN32)
		add_custom_command(TARGET software_renderer POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:SDL3::SDL3>
				$<TARGET_FILE_DIR:software_renderer>
		)
	endif()
