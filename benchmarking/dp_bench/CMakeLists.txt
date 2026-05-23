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

set(FSST8_SOURCES
    "${FSST_ROOT}/libfsst.cpp"
    "${FSST_ROOT}/fsst_avx512.cpp"
    "${FSST_ROOT}/fsst_avx512_unroll1.inc"
    "${FSST_ROOT}/fsst_avx512_unroll2.inc"
    "${FSST_ROOT}/fsst_avx512_unroll3.inc"
    "${FSST_ROOT}/fsst_avx512_unroll4.inc"
)

set(FSST12_SOURCES
    "${FSST_ROOT}/libfsst12.cpp"
)

# ---- 8-bit OptFSST ----------------------------------------------------------
# Production OptFSST: unrolled buildDP with __builtin_expect hints.
add_library(fsst_opt STATIC ${FSST8_SOURCES})
target_include_directories(fsst_opt PUBLIC "${FSST_ROOT}")

# Naive buildDP variant: same source, BUILDDP_NAIVE selects the plain loop in
# SymbolTable::buildDP. The rest of the OptFSST pipeline is unchanged.
add_library(fsst_naive STATIC ${FSST8_SOURCES})
target_include_directories(fsst_naive PUBLIC "${FSST_ROOT}")
target_compile_definitions(fsst_naive PRIVATE BUILDDP_NAIVE=1)

# ---- 12-bit OptFSST12 -------------------------------------------------------
# Production OptFSST12 (4096-symbol dictionary, no AVX-512 path).
add_library(fsst12_opt STATIC ${FSST12_SOURCES})
target_include_directories(fsst12_opt PUBLIC "${FSST_ROOT}")

# Naive buildDP variant of OptFSST12.
add_library(fsst12_naive STATIC ${FSST12_SOURCES})
target_include_directories(fsst12_naive PUBLIC "${FSST_ROOT}")
target_compile_definitions(fsst12_naive PRIVATE BUILDDP_NAIVE=1)

# ---- Bench executables ------------------------------------------------------
# Each binary embeds exactly one (codec x buildDP-variant) pair to avoid
# multiple-definition link errors on the shared Optfsst_* symbols.
add_executable(dp_bench_opt dp_bench.cpp)
target_link_libraries(dp_bench_opt PRIVATE fsst_opt Threads::Threads)

add_executable(dp_bench_naive dp_bench.cpp)
target_link_libraries(dp_bench_naive PRIVATE fsst_naive Threads::Threads)

add_executable(dp_bench_opt12 dp_bench.cpp)
target_compile_definitions(dp_bench_opt12 PRIVATE FSST12=1)
target_link_libraries(dp_bench_opt12 PRIVATE fsst12_opt Threads::Threads)

add_executable(dp_bench_naive12 dp_bench.cpp)
target_compile_definitions(dp_bench_naive12 PRIVATE FSST12=1)
target_link_libraries(dp_bench_naive12 PRIVATE fsst12_naive Threads::Threads)
