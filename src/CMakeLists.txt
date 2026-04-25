set(SWR_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/swrBuffer.h
    ${CMAKE_CURRENT_LIST_DIR}/swrDevice.h
    ${CMAKE_CURRENT_LIST_DIR}/IScene.h
    ${CMAKE_CURRENT_LIST_DIR}/SceneManager.h
    ${CMAKE_CURRENT_LIST_DIR}/PrimitiveTopologyScene.h
    ${CMAKE_CURRENT_LIST_DIR}/TriangleScene.h
    # ${CMAKE_CURRENT_LIST_DIR}/swrMath.h
    # ${CMAKE_CURRENT_LIST_DIR}/swrPipeline.h
    # ${CMAKE_CURRENT_LIST_DIR}/swrTypes.h
)

set(SWR_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PrimitiveTopologyScene.cpp
    ${CMAKE_CURRENT_LIST_DIR}/swrDeviceCore.cpp
    ${CMAKE_CURRENT_LIST_DIR}/swrDeviceDraw.cpp
    ${CMAKE_CURRENT_LIST_DIR}/swrDeviceTiled.cpp
    ${CMAKE_CURRENT_LIST_DIR}/swrDeviceRaster.cpp
    ${CMAKE_CURRENT_LIST_DIR}/SceneManager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/TriangleScene.cpp
)

set(SWR_LIBS
    SDL3::SDL3
    glm::glm
)

if (TARGET TBB::tbb)
    list(APPEND SWR_LIBS TBB::tbb)
endif()

set(SWR_SOURCES_AND_HEADERS
    ${SWR_SOURCES}
    ${SWR_HEADERS}
)

#if OS - Windows and build type - Release add WIN32 subsystem flag
if (WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_executable(software_renderer WIN32 ${SWR_SOURCES_AND_HEADERS})
else()
    add_executable(software_renderer ${SWR_SOURCES_AND_HEADERS})
endif()

target_link_libraries(software_renderer PRIVATE ${SWR_LIBS})

if (TARGET TBB::tbb)
    target_compile_definitions(software_renderer PRIVATE SWR_HAS_TBB=1)
endif()

if (WIN32)
    # Ensure SDL3 runtime DLL is copied next to the executable
    add_custom_command(TARGET software_renderer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:software_renderer>
    )

    if (TARGET TBB::tbb)
        add_custom_command(TARGET software_renderer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:software_renderer>
        )
    endif()
endif()