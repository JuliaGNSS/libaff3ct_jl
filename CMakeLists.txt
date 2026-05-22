cmake_minimum_required(VERSION 3.14)
project(libaff3ct_jl VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Optional SIMD flags (set by BinaryBuilder per micro-architecture)
set(SIMD_FLAGS "" CACHE STRING "SIMD compiler flags (e.g. -mavx2 -mfma)")
if(SIMD_FLAGS)
    string(REPLACE " " ";" SIMD_FLAG_LIST "${SIMD_FLAGS}")
    add_compile_options(${SIMD_FLAG_LIST})
endif()

# ── Find aff3ct ──────────────────────────────────────────────────────
# For local dev:  -Daff3ct_DIR=$HOME/Code/aff3ct/install/lib/cmake/aff3ct-*
# For Yggdrasil:  headers/libs are in ${prefix}
find_package(Threads REQUIRED)
find_package(cpptrace CONFIG QUIET)
find_package(aff3ct CONFIG QUIET)

if(NOT aff3ct_FOUND)
    # Fallback: manual paths (Yggdrasil / custom install)
    set(AFF3CT_INCLUDE_DIR "" CACHE PATH "Path to aff3ct include directory")
    set(AFF3CT_LIBRARY "" CACHE FILEPATH "Path to aff3ct shared library")

    if(AFF3CT_INCLUDE_DIR AND AFF3CT_LIBRARY)
        add_library(aff3ct::aff3ct-static-lib SHARED IMPORTED)
        set_target_properties(aff3ct::aff3ct-static-lib PROPERTIES
            IMPORTED_LOCATION "${AFF3CT_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${AFF3CT_INCLUDE_DIR}")
        set(AFF3CT_TARGET aff3ct::aff3ct-static-lib)
    else()
        message(FATAL_ERROR
            "aff3ct not found via find_package. Set -Daff3ct_DIR=... or "
            "-DAFF3CT_INCLUDE_DIR=... -DAFF3CT_LIBRARY=...")
    endif()
else()
    # aff3ct found via CMake config
    set(AFF3CT_TARGET aff3ct::aff3ct-shared-lib)
endif()

# ── Shared library ───────────────────────────────────────────────────
add_library(aff3ct_jl SHARED
    src/common.cpp
    src/polar.cpp
    src/ldpc.cpp
    src/turbo.cpp
    src/rsc.cpp
    src/conv.cpp
)

target_include_directories(aff3ct_jl
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:include>
)

target_link_libraries(aff3ct_jl PRIVATE ${AFF3CT_TARGET})

# ── Install ──────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS aff3ct_jl
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES src/aff3ct_jl.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ── Test (optional) ──────────────────────────────────────────────────
option(BUILD_TESTING "Build test programs" ON)

if(BUILD_TESTING)
    enable_testing()
    add_executable(test_polar test/test_polar.c)
    target_link_libraries(test_polar PRIVATE aff3ct_jl m)
    target_include_directories(test_polar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME test_polar COMMAND test_polar)
endif()
