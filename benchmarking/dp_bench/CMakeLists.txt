cmake_minimum_required(VERSION 3.10)
project(dp_bench)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# The repo root holds libfsst.cpp / libfsst.hpp and the AVX-512 sources.
set(FSST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

if(CMAKE_BUILD_TYPE STREQUAL Release)
    set_source_files_properties(
        "${FSST_ROOT}/fsst_avx512.cpp"
        PROPERTIES COMPILE_FLAGS -O1
    )
endif()

set(FSST_SOURCES
    "${FSST_ROOT}/libfsst.cpp"
    "${FSST_ROOT}/fsst_avx512.cpp"
    "${FSST_ROOT}/fsst_avx512_unroll1.inc"
    "${FSST_ROOT}/fsst_avx512_unroll2.inc"
    "${FSST_ROOT}/fsst_avx512_unroll3.inc"
    "${FSST_ROOT}/fsst_avx512_unroll4.inc"
)

# Production OptFSST: unrolled buildDP with __builtin_expect hints.
add_library(fsst_opt STATIC ${FSST_SOURCES})
target_include_directories(fsst_opt PUBLIC "${FSST_ROOT}")

# Naive buildDP variant: same source, BUILDDP_NAIVE selects the plain loop in
# SymbolTable::buildDP. The rest of the OptFSST pipeline is unchanged.
add_library(fsst_naive STATIC ${FSST_SOURCES})
target_include_directories(fsst_naive PUBLIC "${FSST_ROOT}")
target_compile_definitions(fsst_naive PRIVATE BUILDDP_NAIVE=1)

add_executable(dp_bench_opt dp_bench.cpp)
target_link_libraries(dp_bench_opt PRIVATE fsst_opt Threads::Threads)

add_executable(dp_bench_naive dp_bench.cpp)
target_link_libraries(dp_bench_naive PRIVATE fsst_naive Threads::Threads)
