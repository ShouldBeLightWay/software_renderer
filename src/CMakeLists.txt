set( SWR_HEADERS ) # Empty for now

set( SWR_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)

set( SWR_LIBS
    SDL3::SDL3
    glm::glm
)

set( SWR_SOURCES_AND_HEADERS
    ${SWR_SOURCES}
    ${SWR_HEADERS}
)

add_executable(  software_renderer ${SWR_SOURCES_AND_HEADERS} )

target_link_libraries(software_renderer PRIVATE ${SWR_LIBS})

    # On Windows, ensure SDL3 runtime DLL is copied next to the executable
	if (WIN32)
		add_custom_command(TARGET software_renderer POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:SDL3::SDL3>
				$<TARGET_FILE_DIR:software_renderer>
		)
	endif()