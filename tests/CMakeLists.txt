cmake_minimum_required(VERSION 3.20)

# Four suites, kept separate because they need different things to run. The hooks, the
# RVAs and the struct offsets are statements about another process's memory and are
# covered by the in-game checklist, not here.

# Pure arithmetic: the UE3 rotation maths, the head-pose composition, the aim vector the
# reticle is placed from, the ADS gate, the field-of-view decision and the zoom scale.
# No sockets, no registry, no Windows headers beyond what the mod's own headers pull in.
add_executable(bsi_tests
    test_main.cpp
)

# The INI: the shipped file's contents, what the reader makes of a hand-edited one, and
# the single-key ADS write-back. Needs the real config reader and writer.
add_executable(bsi_config_tests
    test_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/config.cpp
)

# The tracker port shared with every other mod in the fleet: another process holds it,
# the mod starts anyway, the holder exits, tracking has to come up on its own. Real
# sockets, so it is the one suite with a clock in it.
add_executable(bsi_port_reclaim_tests
    test_port_reclaim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tracking_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/config.cpp
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

foreach(suite bsi_tests bsi_config_tests bsi_port_reclaim_tests bsi_memory_tests)
    target_include_directories(${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(${suite} PRIVATE cameraunlock)
    if(MSVC)
        target_compile_options(${suite} PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME ${suite} COMMAND ${suite})
endforeach()

target_link_libraries(bsi_port_reclaim_tests PRIVATE ws2_32)
