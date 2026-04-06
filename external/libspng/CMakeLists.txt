add_library(spng STATIC spng.c)

target_include_directories(spng PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(spng PRIVATE
    ${zlib_SOURCE_DIR}
    ${zlib_BINARY_DIR}
)

target_link_libraries(spng PRIVATE zlibstatic)

if(MSVC)
    target_compile_options(spng PRIVATE /O2)
else()
    target_compile_options(spng PRIVATE -O3)
endif()