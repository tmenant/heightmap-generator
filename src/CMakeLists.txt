file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h" "*.c")

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_compile_options(${PROJECT_NAME} PRIVATE "-Wa,-mbig-obj")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    cpptrace::cpptrace
    CLI11::CLI11
    pugixml::pugixml
    magic_enum::magic_enum
    FastNoise2
    fmt::fmt
    tinyexr
    spng
)