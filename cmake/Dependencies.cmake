include(FetchContent)

# --- cpptrace ---
FetchContent_Declare(
    cpptrace
    GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
    GIT_TAG        v1.0.4
)

# --- CLI11 ---
FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11
  GIT_TAG        v2.6.2
)

# --- pugixml ---
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.15
)

# --- magic_enum ---
FetchContent_Declare(
    magic_enum
    GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
    GIT_TAG        v0.9.7
)

# --- FastNoise2 ---
FetchContent_Declare(
    FastNoise2
    GIT_REPOSITORY https://github.com/Auburn/FastNoise2.git
    GIT_TAG        v1.0.1
)

# --- fmt ---
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        12.1.0
)

FetchContent_MakeAvailable(
    fmt
    cpptrace
    cli11
    pugixml
    magic_enum
    FastNoise2
)

# --- zlib ---
set(ZLIB_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(ZLIB_BUILD_TESTING OFF CACHE INTERNAL "")
set(ZLIB_BUILD_STATIC ON CACHE INTERNAL "")
set(ZLIB_INSTALL OFF CACHE INTERNAL "")

FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.2
    OVERRIDE_FIND_PACKAGE
)

FetchContent_MakeAvailable(zlib)
