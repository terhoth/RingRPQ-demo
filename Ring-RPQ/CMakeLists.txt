cmake_minimum_required(VERSION 3.10)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
include(AppendCompilerFlags)
if(APPLE)
    # Apple Silicon: without this CMake picks x86_64 and the whole build runs
    # under Rosetta. Inert on every other platform.
    set(CMAKE_OSX_ARCHITECTURES arm64)
endif()
project(Ring-RPQ)
set(CMAKE_PROJECT_NAME "Ring RPQ")

# Release by default (-O3); override with -DCMAKE_BUILD_TYPE=Debug when you
# need symbols. This used to be hardcoded to Debug, which silently shipped
# every binary unoptimized.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# C++11 compiler Check
if(NOT CMAKE_CXX_COMPILER_VERSION) # work around for cmake versions smaller than 2.8.10
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE CMAKE_CXX_COMPILER_VERSION)
endif()
if(CMAKE_CXX_COMPILER MATCHES ".*clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_COMPILER_IS_CLANGXX 1)
endif()
if( (CMAKE_COMPILER_IS_GNUCXX AND ${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 4.7) OR
(CMAKE_COMPILER_IS_CLANGXX AND ${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 3.2))
    message(FATAL_ERROR "Your C++ compiler does not support C++11. Please install g++ 4.7 (or greater) or clang 3.2 (or greater)")
else()
    message(STATUS "Compiler is recent enough to support C++11.")
endif()
if( CMAKE_COMPILER_IS_GNUCXX )
    append_cxx_compiler_flags("-fpermissive -std=c++11 -Wall -Wextra " "GCC" CMAKE_CXX_FLAGS)
    append_cxx_compiler_flags("-msse4.2 -O3 -ffast-math -funroll-loops -fno-omit-frame-pointer -g" "GCC" CMAKE_CXX_FLAGS_RELEASE)
else()
    append_cxx_compiler_flags("-fpermissive -std=c++11" "CLANG" CMAKE_CXX_FLAGS)
    append_cxx_compiler_flags("-stdlib=libc++" "CLANG" CMAKE_CXX_FLAGS)
    append_cxx_compiler_flags("-msse4.2 -O3  -ffast-math -funroll-loops -DNDEBUG" "CLANG" CMAKE_CXX_FLAGS_RELEASE)
endif()
include(CheckSSE)
FindSSE ()
if( SSE4_2_FOUND )
    if( CMAKE_COMPILER_IS_GNUCXX )
        append_cxx_compiler_flags("-msse4.2" "GCC" CMAKE_CXX_FLAGS)
    else()
        append_cxx_compiler_flags("-msse4.2" "CLANG" CMAKE_CXX_FLAGS)
    endif()
    message(STATUS "CPU does support SSE4.2.")
else()
    message(STATUS "CPU does NOT support SSE4.2")
endif()

# The Ring-RPQ fork of SDSL-lite (github.com/adriangbrandon/sdsl-lite), whose
# install.sh defaults to ~/include and ~/lib. Point SDSL_ROOT elsewhere for a
# project-local install.
if(NOT SDSL_ROOT)
    set(SDSL_ROOT "$ENV{HOME}")
endif()

include_directories(${SDSL_ROOT}/include
        ${CMAKE_HOME_DIRECTORY}/nfa/include)

link_directories(${SDSL_ROOT}/lib)

add_subdirectory(nfa)

add_executable(find-paths src/find-paths.cpp)
target_link_libraries(find-paths sdsl divsufsort divsufsort64 nfa)

add_executable(test-correctness src/test-correctness.cpp)
target_link_libraries(test-correctness sdsl divsufsort divsufsort64 nfa)

add_executable(build-index-attr src/build-index-attr.cpp)
target_link_libraries(build-index-attr sdsl divsufsort divsufsort64 nfa)

add_executable(query-index-attr src/query-index-attr.cpp)
target_link_libraries(query-index-attr sdsl divsufsort divsufsort64 nfa)

add_executable(sort-dict src/utilities/sort-pos.cpp)
target_link_libraries(sort-dict sdsl divsufsort divsufsort64 nfa)

add_executable(sort-dict-bfs src/utilities/sort-pos-bfs.cpp)
target_link_libraries(sort-dict-bfs sdsl divsufsort divsufsort64 nfa)

add_executable(queries-prepare src/utilities/queries-prepare.cpp)
target_link_libraries(queries-prepare sdsl divsufsort divsufsort64 nfa)

add_executable(yago-prepare src/utilities/yago-prepare.cpp)
target_link_libraries(yago-prepare sdsl divsufsort divsufsort64)
# libpqxx 7.10 itself needs -std=c++17, but ring_rpq_attr.hpp doesn't compile
# under C++17 (the SDSL fork's wm_int.hpp -> init_array.hpp uses `register`,
# a hard error under strict C++17, though only a deprecation warning under
# this project's normal C++11). So worker.cpp (includes ring_rpq_attr.hpp)
# and worker_db.cpp (includes <pqxx/pqxx>) are separate translation units:
# worker_db.cpp alone gets the -std=c++17 override, worker.cpp builds under
# the project's default flags like every other target.
# libpqxx is discovered three ways, because no single one covers both
# platforms:
#   1. A CMake package config on the default search path — how a
#      from-source install (and Homebrew) exposes itself.
#   2. Debian/Ubuntu's libpqxx-dev (7.10.0 on 26.04) ships *only* a
#      pkg-config .pc file, no CMake config, so find_package can't see it.
#   3. macOS: Homebrew's libpqxx is 8.x, which needs a newer compiler than
#      the rest of this project builds with, so SETUP.md has you build 7.10
#      into ~/.local/libpqxx7 — a path nothing searches by default.
# PQXX_TARGET is whichever import target came back.
find_package(libpqxx CONFIG QUIET)
if(libpqxx_FOUND)
    set(PQXX_TARGET libpqxx::pqxx)
else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PQXX QUIET IMPORTED_TARGET libpqxx)
    endif()
    if(TARGET PkgConfig::PQXX)
        # Ubuntu 26.04's *shared* libpqxx-7.10.so aborts the process with
        # "double free or corruption" during static destruction at exit —
        # reproducible with a six-line program that opens a connection and
        # returns, so it is not something this codebase triggers. The static
        # archive from the same package is unaffected and returns identical
        # results, so prefer it where it exists. Link the shared library
        # instead with -DRINGRPQ_PQXX_STATIC=OFF.
        option(RINGRPQ_PQXX_STATIC "Link libpqxx statically (avoids a double-free at exit in Ubuntu's shared build)" ON)
        find_library(PQXX_STATIC_LIBRARY NAMES libpqxx.a HINTS ${PQXX_LIBRARY_DIRS})
        pkg_check_modules(LIBPQ QUIET IMPORTED_TARGET libpq)
        if(RINGRPQ_PQXX_STATIC AND PQXX_STATIC_LIBRARY AND TARGET PkgConfig::LIBPQ)
            # An INTERFACE target so the archive brings its own include dirs
            # and libpq along, the way an imported target would.
            add_library(ringrpq_pqxx INTERFACE)
            target_include_directories(ringrpq_pqxx INTERFACE ${PQXX_INCLUDE_DIRS})
            target_link_libraries(ringrpq_pqxx INTERFACE ${PQXX_STATIC_LIBRARY} PkgConfig::LIBPQ)
            set(PQXX_TARGET ringrpq_pqxx)
        else()
            set(PQXX_TARGET PkgConfig::PQXX)
        endif()
    else()
        find_package(libpqxx REQUIRED CONFIG PATHS "$ENV{HOME}/.local/libpqxx7" NO_DEFAULT_PATH)
        set(PQXX_TARGET libpqxx::pqxx)
    endif()
endif()
message(STATUS "libpqxx import target: ${PQXX_TARGET}")

add_executable(ring-rpq-worker src/worker.cpp src/worker_db.cpp)
set_source_files_properties(src/worker_db.cpp PROPERTIES COMPILE_OPTIONS "-std=c++17")
target_link_libraries(ring-rpq-worker sdsl divsufsort divsufsort64 nfa ${PQXX_TARGET})
