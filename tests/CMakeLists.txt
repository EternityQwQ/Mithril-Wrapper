add_executable(mithril_core_tests unit/core_tests.cpp)
target_link_libraries(mithril_core_tests PRIVATE mithril_core)
target_compile_definitions(mithril_core_tests PRIVATE MITHRIL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_include_directories(mithril_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(mithril_core_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(mithril_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME mithril_core.unit COMMAND mithril_core_tests)

if(MITHRIL_ENABLE_SHADER_TOOLCHAIN)
    add_executable(mithril_shader_reflection_tests unit/shader_reflection_tests.cpp)
    target_link_libraries(mithril_shader_reflection_tests PRIVATE mithril_shader_toolchain)
    add_test(NAME mithril_shader.reflection COMMAND mithril_shader_reflection_tests)
endif()
add_test(NAME mithril_core.boundary
    COMMAND ${MITHRIL_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_SOURCE_DIR}/tools/quality/scan_for_vulkan.ps1
        -RepositoryRoot ${CMAKE_SOURCE_DIR}
        -BuildDirectory ${CMAKE_BINARY_DIR}
        -RequireTargetArtifact)

if(APPLE AND MITHRIL_BUILD_DIRECT AND MITHRIL_ENABLE_SHADER_TOOLCHAIN)
    add_subdirectory(metal)
    add_test(NAME mithril_direct.boundary
        COMMAND ${MITHRIL_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_SOURCE_DIR}/tools/quality/scan_direct_target.ps1
            -RepositoryRoot ${CMAKE_SOURCE_DIR}
            -BuildDirectory ${CMAKE_BINARY_DIR}
            -RequireTargetArtifact)
endif()
