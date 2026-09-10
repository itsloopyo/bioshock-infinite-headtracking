cmake_minimum_required(VERSION 3.20)
project(BioShockInfiniteHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# BioShock Infinite is a 32-bit UE3 game (Binaries\Win32\BioShockInfinite.exe), and the
# shim is loaded into that process. A 64-bit build produces a DLL the game silently
# refuses to load, which reads in the log as "the mod never started".
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "BioShockInfiniteHeadTracking must be built as Win32 (x86). Configure with -A Win32.")
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    # Static CRT so the shipped xinput1_3.dll runs without a VC++ redistributable. The
    # game is from 2013 and ships its own MSVC 9 runtime; ours must not depend on a
    # newer one being present.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Vendored rather than fetched: `pixi run package` has to succeed on a clean checkout
# with no network and no game installed, which a FetchContent at configure time does not.
set(MINHOOK_SOURCES
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)

set(MOD_SOURCES
    src/dllmain.cpp
    src/xinput_proxy.cpp
    src/ads_state.cpp
    src/aim_marker.cpp
    src/build_profile.cpp
    src/camera_hook.cpp
    src/config.cpp
    src/fov_override.cpp
    src/game_crosshair.cpp
    src/game_state.cpp
    src/hotkeys.cpp
    src/path_utils.cpp
    src/scene_view.cpp
    src/state_probe.cpp
    src/tracking_runtime.cpp
    src/ue3_object.cpp
    src/window_center.cpp
    src/zoom_scale.cpp
)

add_library(BioShockInfiniteHeadTracking SHARED
    ${MINHOOK_SOURCES}
    ${MOD_SOURCES}
)

# Shipped AS xinput1_3.dll. The game statically imports XINPUT1_3.dll and the exe's own
# directory is searched before the system one, so dropping this beside the exe is the
# entire loading mechanism. src/xinput1_3.def re-exports the ordinals the game binds.
set_target_properties(BioShockInfiniteHeadTracking PROPERTIES
    OUTPUT_NAME "xinput1_3"
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/Release"
)
target_link_options(BioShockInfiniteHeadTracking PRIVATE
    "/DEF:${CMAKE_SOURCE_DIR}/src/xinput1_3.def")

target_compile_definitions(BioShockInfiniteHeadTracking PRIVATE
    HEADTRACKING_VERSION="${PROJECT_VERSION}")

target_include_directories(BioShockInfiniteHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)

target_link_libraries(BioShockInfiniteHeadTracking PRIVATE
    cameraunlock
    ws2_32
)

if(MSVC)
    target_compile_options(BioShockInfiniteHeadTracking PRIVATE /W4 /WX /permissive- /wd4324)
    set_source_files_properties(${MINHOOK_SOURCES} PROPERTIES COMPILE_FLAGS "/W0")
endif()

option(BSI_BUILD_TESTS "Build the mod's own unit tests" ON)
if(BSI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
