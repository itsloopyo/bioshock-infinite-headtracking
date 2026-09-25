cmake_minimum_required(VERSION 3.20)

# Four suites, kept separate because they need different things to run. The hooks, the
# RVAs and the struct offsets are statements about another process's memory and are
# covered by the in-game checklist, not here.

# Pure arithmetic: the UE3 rotation maths, the head-pose composition, the aim vector the
# reticle is placed from, the tracking gate and the ADS lean ease, the field-of-view
# decision and the zoom scale.
# No sockets, no registry, no Windows headers beyond what the mod's own headers pull in.
add_executable(bsi_tests
    test_main.cpp
)

# The INI in the canonical format: the committed file against the table (its
# --render-config mode is what `pixi run render-config` runs), what the table reads, and
# the rows a hotkey may save. Needs the real config owner on real files.
add_executable(bsi_config_tests
    test_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/legacy_config/legacy_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/path_utils.cpp
)

# The tracker port shared with every other mod in the fleet: another process holds it,
# the mod starts anyway, the holder exits, tracking has to come up on its own. Real
# sockets, so it is the one suite with a clock in it.
add_executable(bsi_port_reclaim_tests
    test_port_reclaim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracking_runtime.cpp
)

# The two page predicates every read of another process's memory goes through: the one in
# front of a profile-supplied address (memory_probe.h) and the cached one the object walk
# uses (ue3_object.cpp). Real pages from the OS, so it is Windows-only - which is why it is
# not in bsi_tests, whose whole point is that it builds anywhere the maths does.
#
# Only the part of ue3_object.cpp below the build profile is reachable here: the region
# cache, the guarded copy and the dword accessors. Everything above them needs the game's
# own name pool, which is covered by the in-game checklist.
add_executable(bsi_memory_tests
    test_memory_probe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ue3_object.cpp
)

# The config conversion's differential test: the dev build's reader (the oracle, byte copies
# of 128e9d4's files in config_differential/oracle) against the frozen reader in
# src/legacy_config, and that against the conversion itself. The oracle is compiled with its namespace renamed so its Config can sit
# beside the runtime's in one binary, and against the core ADS headers it included at its
# pin, which core has since removed.
set(BSI_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config_differential)
add_executable(bsi_config_differential_tests
    ${BSI_DIFFERENTIAL_DIR}/differential_test.cpp
    ${BSI_DIFFERENTIAL_DIR}/oracle/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/legacy_config/legacy_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/path_utils.cpp
)
set_source_files_properties(${BSI_DIFFERENTIAL_DIR}/oracle/config.cpp PROPERTIES
    COMPILE_DEFINITIONS "BioShockInfiniteHeadTracking=BsiOracle128e9d4")
target_include_directories(bsi_config_differential_tests BEFORE PRIVATE
    ${BSI_DIFFERENTIAL_DIR}/oracle/core-c480d8a)
target_include_directories(bsi_config_differential_tests PRIVATE ${BSI_DIFFERENTIAL_DIR})
target_compile_definitions(bsi_config_differential_tests PRIVATE
    BSI_DIFFERENTIAL_DATA_DIR="${BSI_DIFFERENTIAL_DIR}/data")

# What the differential test compiles is pinned by hash, line endings aside: the oracle's
# files must stay the published build's (git show 128e9d4:src/<file>, and core c480d8a's
# ADS headers), the core sources both readers compile must stay the ones that build was
# made with (core c480d8a), and the frozen reader is never edited once it is committed.
function(bsi_require_hash path expected)
    file(READ "${path}" text)
    string(REPLACE "\r" "" text "${text}")
    string(SHA256 actual "${text}")
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                            "${expected}). The config differential test compares against "
                            "exactly this file; see tests/CMakeLists.txt.")
    endif()
endfunction()
set(BSI_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cameraunlock-core/cpp)
foreach(pinned
        "${BSI_DIFFERENTIAL_DIR}/oracle/ads.h|941e1b0418a731abc33c12a12d4e86b2a42e0b0c596dc079036ae8d3c0391667"
        "${BSI_DIFFERENTIAL_DIR}/oracle/config.cpp|cf03a56e1e6adbf147cede47d5b6ea202720cbebcdfd276565389ffc9229d055"
        "${BSI_DIFFERENTIAL_DIR}/oracle/config.h|a53911e34f802e82f16c00f33a0ca6c68d0c692ccb2a7aeab979a0d554683868"
        "${BSI_DIFFERENTIAL_DIR}/oracle/config_sanitize.h|0794e17bb7095b3cca1d7399829b751b7cb3b8070fb704abd9e2ae9bff5af68b"
        "${BSI_DIFFERENTIAL_DIR}/oracle/logging.h|49e56a4d48fcef56ef40434be88a68f42697fecd392b3accd230bbdaa030f78f"
        "${BSI_DIFFERENTIAL_DIR}/oracle/core-c480d8a/cameraunlock/ads/ads_blend.h|bcc009fa97e0d8284a46ed5284ec0741ad3f1e8d1babe4be07bf45476351560a"
        "${BSI_DIFFERENTIAL_DIR}/oracle/core-c480d8a/cameraunlock/ads/ads_fade.h|00b80e59d261546dd50138759676f5a1b0d07681fa79a9b89be69797dc35c37f"
        "${BSI_DIFFERENTIAL_DIR}/oracle/core-c480d8a/cameraunlock/ads/ads_mode.h|94cd36b585e878e673566f602e9496417cb797970162fcc9c2af1e50e24358de"
        "${BSI_DIFFERENTIAL_DIR}/oracle/core-c480d8a/cameraunlock/ads/entry_pose.h|0c26c26fd3f6ba8307b731af391340e9286504ef157329c04883495f211cc870"
        "${BSI_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
        "${BSI_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
        "${BSI_CORE_DIR}/include/cameraunlock/data/position_settings.h|b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"
        "${BSI_CORE_DIR}/include/cameraunlock/math/smoothing_utils.h|fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/legacy_config/legacy_config.h|05fe2cbe316aa32c6234234f9fc1666fd65d4f59150b24dc28c08f46df3c581b"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/legacy_config/legacy_config.cpp|ab7908db06d44c298800468b0bba709089a46733e7ddcf3e50bb57e253501782"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/legacy_config/legacy_sanitize.h|7ecee8bf58c1a73060f03a7eb077e6c1c2814ff5177d68583deda9710b525323")
    string(REPLACE "|" ";" pair "${pinned}")
    list(GET pair 0 pinned_path)
    list(GET pair 1 pinned_hash)
    bsi_require_hash("${pinned_path}" "${pinned_hash}")
endforeach()

foreach(suite bsi_tests bsi_config_tests bsi_port_reclaim_tests bsi_memory_tests bsi_config_differential_tests)
    target_include_directories(${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(${suite} PRIVATE cameraunlock)
    if(MSVC)
        target_compile_options(${suite} PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME ${suite} COMMAND ${suite})
endforeach()

target_link_libraries(bsi_port_reclaim_tests PRIVATE ws2_32)

# The file `pixi run render-config` writes, at the path core's data/config-format.json
# records for this repo.
foreach(suite bsi_config_tests bsi_config_differential_tests)
    target_compile_definitions(${suite} PRIVATE
        BSI_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/../config/HeadTracking.ini")
endforeach()
