cmake_minimum_required(VERSION 3.26)

project(hmapgen VERSION 0.1.0 LANGUAGES CXX C)

set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/dependencies")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "force all external libs to build in static mode")

include(cmake/Dependencies.cmake)

add_subdirectory(external/libspng)
add_subdirectory(src)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/compile_commands.json"
                "${CMAKE_SOURCE_DIR}/.vscode/compile_commands.json"
        COMMENT "Updating compile_commands.json for clangd"
    )
endif()