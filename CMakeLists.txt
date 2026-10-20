cmake_minimum_required(VERSION 3.21)
project(Oni3Patch LANGUAGES CXX)

# The game is a 32-bit executable: configure with a Win32 generator, e.g.
#   cmake -S . -B build -A Win32
#   cmake --build build --config Release
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Oni3Patch targets Windows with MSVC (the dsound proxy .def uses MSVC x86 decorated names).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Oni3Patch must be built as 32-bit (use -A Win32).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source folders
set(ONI_PATCH_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src/Oni3Patch")
set(ONI_DSOUND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/dsound")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# Microsoft Detours: bundled NuGet package in ThirdParty/ by default.
# Override with -DDETOURS_INCLUDE_DIR=... and -DDETOURS_LIBRARY=<path to detours.lib>.
set(DETOURS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Detours.4.0.1/lib/native")
find_path(DETOURS_INCLUDE_DIR detours.h
    HINTS "${DETOURS_ROOT}/include"
    NO_DEFAULT_PATH)
find_library(DETOURS_LIBRARY NAMES detours
    HINTS "${DETOURS_ROOT}/libs"
    PATH_SUFFIXES x86 Win32
    NO_DEFAULT_PATH)

if(NOT DETOURS_INCLUDE_DIR OR NOT DETOURS_LIBRARY)
    # Fall back to vcpkg / system install
    find_package(detours CONFIG QUIET)
    if(TARGET Microsoft::Detours)
        set(ONI_DETOURS_TARGET Microsoft::Detours)
    else()
        message(FATAL_ERROR "Detours not found. Expected ${DETOURS_ROOT}/include/detours.h and "
                            "${DETOURS_ROOT}/libs/x86/detours.lib, or set DETOURS_INCLUDE_DIR and DETOURS_LIBRARY.")
    endif()
else()
    add_library(oni_detours INTERFACE)
    target_include_directories(oni_detours INTERFACE "${DETOURS_INCLUDE_DIR}")
    target_link_libraries(oni_detours INTERFACE "${DETOURS_LIBRARY}")
    set(ONI_DETOURS_TARGET oni_detours)
endif()

# Legacy DirectX SDK (June 2010): its d3d9.h exposes the helper members used by
# MyIDirect3D9 / MyIDirect3DDevice9 when D3D_DEBUG_INFO is defined.
set(DXSDK_DIR "" CACHE PATH "Path to the DirectX SDK (June 2010), e.g. C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)")
find_path(DXSDK_INCLUDE_DIR d3d9.h
    HINTS
        "${DXSDK_DIR}/Include"
        "$ENV{DXSDK_DIR}/Include"
        "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
        "C:/Program Files/Microsoft DirectX SDK (June 2010)/Include"
    NO_DEFAULT_PATH)
if(NOT DXSDK_INCLUDE_DIR)
    message(FATAL_ERROR "DirectX SDK (June 2010) d3d9.h not found.\n"
        "  DXSDK_DIR (cache) = '${DXSDK_DIR}'\n"
        "  DXSDK_DIR (env)   = '$ENV{DXSDK_DIR}'\n"
        "Pass -DDXSDK_DIR=<sdk root> (the folder that contains Include\\), and delete "
        "CMakeCache.txt if a previous configure cached a wrong value.")
endif()
message(STATUS "DirectX SDK headers: ${DXSDK_INCLUDE_DIR}")

file(STRINGS "${DXSDK_INCLUDE_DIR}/d3d9.h" _oni_dbg_info REGEX "D3D_DEBUG_INFO")
if(NOT _oni_dbg_info)
    message(WARNING "${DXSDK_INCLUDE_DIR}/d3d9.h has no D3D_DEBUG_INFO section; "
                    "MyIDirect3D9/MyIDirect3DDevice9 will not compile against it.")
endif()

# framework.h includes <d3d9helper.h>, which is not part of the DirectX SDK. If your
# project ships its own copy, put its folder in ONI_EXTRA_INCLUDE_DIRS.
set(ONI_EXTRA_INCLUDE_DIRS "" CACHE STRING "Extra include directories (e.g. where d3d9helper.h lives)")

# ---------------------------------------------------------------------------
# Oni3Patch.dll
# ---------------------------------------------------------------------------
add_library(Oni3Patch SHARED
    ${ONI_PATCH_DIR}/dllmain.cpp
    ${ONI_PATCH_DIR}/MyIDirect3D9.cpp
    ${ONI_PATCH_DIR}/MyIDirect3DDevice9.cpp
    ${ONI_PATCH_DIR}/OniHook.cpp
    ${ONI_PATCH_DIR}/Trace.cpp
    ${ONI_PATCH_DIR}/Utils.cpp
    ${ONI_PATCH_DIR}/pch.cpp

    ${ONI_PATCH_DIR}/Config.h
    ${ONI_PATCH_DIR}/dllmain.h
    ${ONI_PATCH_DIR}/framework.h
    ${ONI_PATCH_DIR}/MyIDirect3D9.h
    ${ONI_PATCH_DIR}/MyIDirect3DDevice9.h
    ${ONI_PATCH_DIR}/OniHook.h
    ${ONI_PATCH_DIR}/pch.h
    ${ONI_PATCH_DIR}/Trace.h
    ${ONI_PATCH_DIR}/Utils.h
)

target_include_directories(Oni3Patch PRIVATE
    "${ONI_PATCH_DIR}"
    "${DXSDK_INCLUDE_DIR}"
    ${ONI_EXTRA_INCLUDE_DIRS}
)
target_compile_definitions(Oni3Patch PRIVATE
    UNICODE
    _UNICODE                # Visual Studio projects define these by default; CMake doesn't
    WIN32_LEAN_AND_MEAN
    D3D_DEBUG_INFO          # exposes the helper members used by the wrappers
    $<$<CONFIG:Debug>:_DEBUG>
)
target_compile_options(Oni3Patch PRIVATE /W3 /permissive- /utf-8)
target_link_libraries(Oni3Patch PRIVATE ${ONI_DETOURS_TARGET})
target_precompile_headers(Oni3Patch PRIVATE "${ONI_PATCH_DIR}/pch.h")
set_target_properties(Oni3Patch PROPERTIES
    OUTPUT_NAME "Oni3Patch"
    PREFIX ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"   # static CRT: nothing extra to ship
)

# ---------------------------------------------------------------------------
# dsound.dll proxy (loads Oni3Patch.dll from its own folder)
# ---------------------------------------------------------------------------
add_library(dsound_proxy SHARED
    ${ONI_DSOUND_DIR}/dsound.cpp
    ${ONI_DSOUND_DIR}/dsound.def
)
target_compile_definitions(dsound_proxy PRIVATE WIN32_LEAN_AND_MEAN)
target_compile_options(dsound_proxy PRIVATE /W3 /permissive- /utf-8)
set_target_properties(dsound_proxy PROPERTIES
    OUTPUT_NAME "dsound"
    PREFIX ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
add_dependencies(dsound_proxy Oni3Patch)

add_custom_command(TARGET dsound_proxy POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/Oni3Patch.ini $<TARGET_FILE_DIR:dsound_proxy>)