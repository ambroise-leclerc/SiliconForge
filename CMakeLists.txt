cmake_minimum_required(VERSION 3.31)

project(SiliconForge
    VERSION 0.1.0
    DESCRIPTION "Microcontroller emulator for fast firmware iteration, with a WebFront user interface"
    LANGUAGES CXX)

option(SILICONFORGE_BUILD_APP "Build the siliconforge application and its WebFront interface" ${PROJECT_IS_TOP_LEVEL})
option(SILICONFORGE_BUILD_TESTS "Build the SiliconForge tests" ${PROJECT_IS_TOP_LEVEL})
option(SILICONFORGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 26)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    # No C++20 modules here; module scanning adds -fmodules-ts, which makes GCC 16 crash on stdexec (WebFront).
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    # `mkdir build && cd build && cmake .. && cmake --build .` gives an optimised build.
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(CPM)
include(Warnings)

add_library(SiliconForge_options INTERFACE)
target_compile_features(SiliconForge_options INTERFACE cxx_std_26)
siliconforge_set_warnings(SiliconForge_options)

add_subdirectory(src)

if(SILICONFORGE_BUILD_APP)
    # The application opens a native window (WebKitGTK, WebView2 or WKWebView) when WebFront finds the platform's
    # engine, and the system browser otherwise. -DWEBFRONT_EMBED_WEBKITGTK=OFF (or the platform's option) forces the
    # browser.
    set(WEBFRONT_AUTODETECT_NATIVE ON CACHE BOOL "Enable the native window engine of this platform when found")
    # Override with -DCPM_WebFront_SOURCE=/path/to/WebFront to work on a local checkout.
    CPMAddPackage(
        NAME WebFront
        GITHUB_REPOSITORY ambroise-leclerc/WebFront
        GIT_TAG fd4109570f572d1cddad04b5b6307ba4539dcbff)
    add_subdirectory(apps/siliconforge)
endif()

if(SILICONFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
