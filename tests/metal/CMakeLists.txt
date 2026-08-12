add_executable(mithril_metal_tests MetalTestHost.mm)
set_source_files_properties(MetalTestHost.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
target_include_directories(mithril_metal_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(mithril_metal_tests PRIVATE mithril_direct mithril_shader_toolchain
    "-framework Metal" "-framework Foundation" "-framework QuartzCore")
add_test(NAME mithril_metal.smoke COMMAND mithril_metal_tests)
