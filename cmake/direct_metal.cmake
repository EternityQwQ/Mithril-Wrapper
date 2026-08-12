set(MITHRIL_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Mithril-Wrapper-cpp")

add_library(mithril_core STATIC
    ${MITHRIL_CORE_ROOT}/src/abi/AbiManifest.cpp
    ${MITHRIL_CORE_ROOT}/src/abi/ProcTable.cpp
    ${MITHRIL_CORE_ROOT}/src/core/Error.cpp
    ${MITHRIL_CORE_ROOT}/src/gl/Capability.cpp
    ${MITHRIL_CORE_ROOT}/src/gl/Context.cpp
    ${MITHRIL_CORE_ROOT}/src/ir/PipelineKey.cpp
    ${MITHRIL_CORE_ROOT}/src/shader/GlslPreprocessor.cpp
    ${MITHRIL_CORE_ROOT}/src/shader/ShaderTypes.cpp
)

target_include_directories(mithril_core PUBLIC ${MITHRIL_CORE_ROOT}/src)
target_compile_features(mithril_core PUBLIC cxx_std_20)
set_target_properties(mithril_core PROPERTIES CXX_EXTENSIONS OFF)

get_target_property(_mithril_core_sources mithril_core SOURCES)
get_target_property(_mithril_core_links mithril_core LINK_LIBRARIES)
foreach(_item IN LISTS _mithril_core_sources _mithril_core_links)
    string(TOLOWER "${_item}" _item_lower)
    if(_item_lower MATCHES "vulkan|moltenvk|directvulkan|metal|\.mm$")
        message(FATAL_ERROR "mithril_core boundary violation: ${_item}")
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mithril_core.boundary.json"
    CONTENT "{\"target\":\"mithril_core\",\"sources\":\"$<JOIN:$<TARGET_PROPERTY:mithril_core,SOURCES>,;>\",\"links\":\"$<JOIN:$<TARGET_PROPERTY:mithril_core,LINK_LIBRARIES>,;>\"}\n")

if(MSVC)
    target_compile_options(mithril_core PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(mithril_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(mithril_abi_manifest_tool
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi/gen_manifest.cpp
)
target_link_libraries(mithril_abi_manifest_tool PRIVATE mithril_core)

if(MITHRIL_ENABLE_SHADER_TOOLCHAIN)
    add_library(mithril_shader_toolchain STATIC
        ${MITHRIL_CORE_ROOT}/src/shader/GlslangCompiler.cpp
        ${MITHRIL_CORE_ROOT}/src/shader/SpirvCrossMslCompiler.cpp
    )
    target_include_directories(mithril_shader_toolchain PUBLIC ${MITHRIL_CORE_ROOT}/src)
    target_compile_features(mithril_shader_toolchain PUBLIC cxx_std_20)
    target_link_libraries(mithril_shader_toolchain PRIVATE
        mithril_core
        glslang::glslang
        glslang::SPIRV
        glslang::glslang-default-resource-limits
        spirv-cross-core
        spirv-cross-glsl
        spirv-cross-msl
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_compile_definitions(mithril_shader_toolchain PRIVATE MITHRIL_TARGET_IOS=1)
    endif()
endif()

if(APPLE AND MITHRIL_BUILD_DIRECT)
    set(MITHRIL_DIRECT_SOURCES
        ${MITHRIL_CORE_ROOT}/src/egl/EglConfig.cpp
        ${MITHRIL_CORE_ROOT}/src/egl/EglRuntime.mm
        ${MITHRIL_CORE_ROOT}/src/metal/DeferredReleaseQueue.cpp
        ${MITHRIL_CORE_ROOT}/src/metal/FrameScheduler.mm
        ${MITHRIL_CORE_ROOT}/src/metal/MetalDeviceSession.mm
        ${MITHRIL_CORE_ROOT}/src/metal/MetalShaderLibraryCompiler.mm
        ${MITHRIL_CORE_ROOT}/src/platform/apple/AppleCapabilities.mm
        ${MITHRIL_CORE_ROOT}/src/platform/apple/AppleSurface.mm
    )
    add_library(mithril_direct SHARED ${MITHRIL_DIRECT_SOURCES})
    target_include_directories(mithril_direct PUBLIC
        ${MITHRIL_CORE_ROOT}/src
        ${MITHRIL_CORE_ROOT}/include)
    target_compile_features(mithril_direct PUBLIC cxx_std_20)
    target_link_libraries(mithril_direct PRIVATE mithril_core
        "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(mithril_direct PRIVATE "-framework UIKit")
    else()
        target_link_libraries(mithril_direct PRIVATE "-framework AppKit")
    endif()
    set_source_files_properties(
        ${MITHRIL_CORE_ROOT}/src/egl/EglRuntime.mm
        ${MITHRIL_CORE_ROOT}/src/metal/FrameScheduler.mm
        ${MITHRIL_CORE_ROOT}/src/metal/MetalDeviceSession.mm
        ${MITHRIL_CORE_ROOT}/src/metal/MetalShaderLibraryCompiler.mm
        ${MITHRIL_CORE_ROOT}/src/platform/apple/AppleCapabilities.mm
        ${MITHRIL_CORE_ROOT}/src/platform/apple/AppleSurface.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_target_properties(mithril_direct PROPERTIES
        OUTPUT_NAME mithril PREFIX "lib" SUFFIX ".dylib"
        CXX_EXTENSIONS OFF OBJCXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(mithril_direct PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    get_target_property(_mithril_direct_sources mithril_direct SOURCES)
    get_target_property(_mithril_direct_links mithril_direct LINK_LIBRARIES)
    foreach(_item IN LISTS _mithril_direct_sources _mithril_direct_links)
        string(TOLOWER "${_item}" _item_lower)
        if(_item_lower MATCHES "vulkan|moltenvk|directvulkan")
            message(FATAL_ERROR "mithril_direct boundary violation: ${_item}")
        endif()
    endforeach()
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mithril_direct.boundary.json"
        CONTENT "{\"target\":\"mithril_direct\",\"sources\":\"$<JOIN:$<TARGET_PROPERTY:mithril_direct,SOURCES>,;>\",\"links\":\"$<JOIN:$<TARGET_PROPERTY:mithril_direct,LINK_LIBRARIES>,;>\"}\n")
endif()

if(MITHRIL_BUILD_TESTS)
    include(cmake/testing.cmake)
endif()
