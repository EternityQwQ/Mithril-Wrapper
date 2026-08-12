include(CTest)
enable_testing()

find_program(MITHRIL_POWERSHELL NAMES pwsh powershell REQUIRED)

option(MITHRIL_ENABLE_SANITIZERS "Enable address and undefined sanitizers" OFF)

add_subdirectory(tests)

if(MITHRIL_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    foreach(target mithril_core mithril_core_tests)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()
