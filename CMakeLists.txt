cmake_minimum_required(VERSION 3.5)

project(btrfsst)

option(FSST_DISABLE_AVX512 "Disable AVX-512 code paths and runtime dispatch" OFF)
option(FSST_USE_TRIE_IMPL "Build FSST with the trie-based library implementation" OFF)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_VERBOSE_MAKEFILE ON)

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
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O3 /DNDEBUG")
endif()

if(CMAKE_BUILD_TYPE STREQUAL Release)
set_source_files_properties( 
    fsst_avx512.cpp 
    PROPERTIES
    COMPILE_FLAGS -O1
)
endif()

set(FSST_LIBRARY_SOURCE libfsst.cpp)
if(FSST_USE_TRIE_IMPL)
    set(FSST_LIBRARY_SOURCE libfsst_trie.cpp)
endif()

add_library(fsst ${FSST_LIBRARY_SOURCE} fsst_avx512.cpp fsst_avx512_unroll1.inc fsst_avx512_unroll2.inc fsst_avx512_unroll3.inc fsst_avx512_unroll4.inc)
add_executable(binary fsst.cpp)
target_link_libraries (binary LINK_PUBLIC fsst)
target_link_libraries (binary LINK_PUBLIC Threads::Threads)
set_target_properties(binary PROPERTIES OUTPUT_NAME fsst)

if(FSST_DISABLE_AVX512)
    target_compile_definitions(fsst PRIVATE FSST_DISABLE_AVX512=1)
    target_compile_definitions(binary PRIVATE FSST_DISABLE_AVX512=1)
    target_compile_options(fsst PRIVATE -mno-avx512f -mno-avx512bw -mno-avx512vl -mno-avx512dq -mno-avx512cd)
    target_compile_options(binary PRIVATE -mno-avx512f -mno-avx512bw -mno-avx512vl -mno-avx512dq -mno-avx512cd)
endif()


add_library(fsst12 libfsst12.cpp)
add_executable(binary12 fsst.cpp)
target_link_libraries (binary12 LINK_PUBLIC fsst12)
target_link_libraries (binary12 LINK_PUBLIC Threads::Threads)
set_target_properties(binary12 PROPERTIES OUTPUT_NAME fsst12)
set_target_properties(binary12 PROPERTIES COMPILE_FLAGS -DFSST12)
