cmake_minimum_required(VERSION 3.28)
project(pylinkaudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Suppress deprecation warnings from older CMake projects we vendor (e.g. Link).
if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

# ---- Ableton Link (header-only, dual-licensed GPLv2+ / commercial) ----
# Pinned to Link-4.0.0b4 (April 14 2026). Bump to Link-4.0.0 on May 5.
# See cmake/link-version.cmake for SHA pin.
include(${CMAKE_SOURCE_DIR}/cmake/link-version.cmake)

FetchContent_Declare(
    ableton_link
    GIT_REPOSITORY https://github.com/Ableton/link.git
    GIT_TAG ${PYLINKAUDIO_LINK_TAG}
    GIT_SHALLOW TRUE
    # Suppress Link's own add_subdirectory by pointing SOURCE_SUBDIR at a
    # nonexistent path. We include AbletonLinkConfig.cmake directly below so
    # the `Ableton::Link` IMPORTED INTERFACE target lives in OUR scope (when
    # ConfigureAbletonLink() runs inside a subdirectory, the target is not
    # visible to parent scopes).
    SOURCE_SUBDIR _skip_subdir_
    SYSTEM
)
FetchContent_MakeAvailable(ableton_link)

# Create the Ableton::Link target in our scope.
include(${ableton_link_SOURCE_DIR}/AbletonLinkConfig.cmake)

# ---- pybind11 (provided via build-system.requires) ----
find_package(pybind11 CONFIG REQUIRED)

# ---- The compiled extension ----
pybind11_add_module(_core
    src/_core/module.cpp
    src/_core/link_core.cpp
    src/_core/link_audio.cpp
)

# Link's CMake exposes Ableton::Link as an INTERFACE target carrying
# include paths, asio, and platform link flags (CoreFoundation on macOS,
# pthreads on Linux).
target_link_libraries(_core PRIVATE Ableton::Link)

# Make the module discoverable as pylinkaudio._core
install(TARGETS _core LIBRARY DESTINATION pylinkaudio)
