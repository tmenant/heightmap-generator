add_library(tinyexr STATIC tinyexr.cpp)

target_include_directories(tinyexr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(tinyexr PRIVATE
    ${zlib_SOURCE_DIR}
    ${zlib_BINARY_DIR}
)

target_link_libraries(tinyexr PUBLIC zlibstatic)

if(MSVC)
    target_compile_options(tinyexr PRIVATE /O2)
else()
    target_compile_options(tinyexr PRIVATE -O3)
endif()