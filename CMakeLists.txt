cmake_minimum_required(VERSION 3.20)
project(RuneC C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()
find_package(ZLIB REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME test_portable_paths
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_portable_paths.py)
add_test(NAME validate_no_external_repos
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/validate_no_external_repos.py)
add_test(NAME test_loot_sources
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_loot_sources.py)
add_test(NAME test_weapon_metadata
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_weapon_metadata.py)
add_test(NAME test_magic_assets
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_magic_assets.py)
add_test(NAME test_spell_icon_assets
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_spell_icon_assets.py)
add_test(NAME test_item_sprite_assets
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_item_sprite_assets.py)
add_test(NAME test_prayer_icon_assets
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_prayer_icon_assets.py)
add_test(NAME test_storage_policy
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_storage_policy.py)

# rc-core: generic game backend library (no render deps, no OSRS-specific
# content). See rc-core/README.md for the engine/content split.
add_custom_target(validate-direct-global-access
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/validate_direct_global_access.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Validating migrated runtime global access"
)
add_test(
    NAME validate_direct_global_access
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/validate_direct_global_access.py
)
set_tests_properties(validate_direct_global_access PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME test_region_sets
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_region_sets.py
)
set_tests_properties(test_region_sets PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME test_mapsquare_exports
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_mapsquare_exports.py
)
set_tests_properties(test_mapsquare_exports PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME test_spawn_index
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_spawn_index.py
)
set_tests_properties(test_spawn_index PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME test_object_placement_index
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_object_placement_index.py
)
set_tests_properties(test_object_placement_index PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME test_collision_tile_index
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_collision_tile_index.py
)
set_tests_properties(test_collision_tile_index PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

file(GLOB RC_CORE_SOURCES rc-core/*.c)
add_library(rc-core STATIC ${RC_CORE_SOURCES})
add_dependencies(rc-core validate-direct-global-access)
target_include_directories(rc-core PUBLIC rc-core)
target_link_libraries(rc-core PUBLIC ZLIB::ZLIB)

# rc-content: OSRS-specific content (boss scripts, quests, region code).
# Links against rc-core. Isolation targets select a subset of these .c
# files. See rc-content/README.md.
file(GLOB_RECURSE RC_CONTENT_SOURCES CONFIGURE_DEPENDS rc-content/*.c)
add_library(rc-content-objects OBJECT ${RC_CONTENT_SOURCES})
target_include_directories(rc-content-objects PUBLIC rc-content)
target_link_libraries(rc-content-objects PUBLIC rc-core)

add_library(rc-content STATIC $<TARGET_OBJECTS:rc-content-objects>)
target_include_directories(rc-content PUBLIC rc-content)
target_link_libraries(rc-content PUBLIC rc-core)

add_library(rc-content-link INTERFACE)
add_dependencies(rc-content-link rc-content-objects)
target_include_directories(rc-content-link INTERFACE rc-content)
target_link_libraries(rc-content-link INTERFACE
    "$<TARGET_OBJECTS:rc-content-objects>" rc-core)

# rc-viewer: raylib frontend (links both libraries).
if(EXISTS "${CMAKE_SOURCE_DIR}/lib/raylib/lib/libraylib.a")
    file(GLOB RC_VIEWER_SOURCES CONFIGURE_DEPENDS rc-viewer/*.c)
    add_executable(rc-viewer ${RC_VIEWER_SOURCES})
    target_link_libraries(rc-viewer rc-core rc-content-link)
    target_include_directories(rc-viewer PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/raylib/include
    )
    target_link_directories(rc-viewer PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/raylib/lib
    )
    target_link_libraries(rc-viewer raylib m dl pthread)

    # Platform-specific libs
    if(UNIX AND NOT APPLE)
        find_package(OpenGL REQUIRED)
        target_link_libraries(rc-viewer OpenGL::GL)
    endif()
else()
    message(WARNING "Raylib not found at lib/raylib/ — skipping rc-viewer build. "
                    "Place prebuilt raylib in lib/raylib/{include,lib}/")
endif()

# Tests — link both libraries so content-module register fns resolve.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.c)
foreach(TEST_SRC ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_link_libraries(${TEST_NAME} rc-core rc-content-link m)
    target_compile_definitions(${TEST_NAME} PRIVATE
        RC_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TEST_NAME} PRIVATE -UNDEBUG)
    elseif(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /UNDEBUG)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endforeach()

if(TARGET test_runtime_foundation)
    target_link_libraries(test_runtime_foundation pthread)
endif()

if(TARGET test_combat_visuals_projectiles)
    target_sources(test_combat_visuals_projectiles PRIVATE
        rc-viewer/combat_visuals.c
    )
    target_include_directories(test_combat_visuals_projectiles PRIVATE
        rc-viewer
    )
endif()
if(TARGET test_ralos_runtime)
    target_sources(test_ralos_runtime PRIVATE rc-viewer/combat_visuals.c)
endif()

if(TARGET test_items_bin)
    target_sources(test_items_bin PRIVATE
        rc-viewer/item_render_defs.c
    )
    target_include_directories(test_items_bin PRIVATE
        rc-viewer
    )
endif()

if(TARGET test_npc_defs_bin)
    target_sources(test_npc_defs_bin PRIVATE
        rc-viewer/npc_render_defs.c
    )
    target_include_directories(test_npc_defs_bin PRIVATE
        rc-viewer
    )
endif()

if(TARGET test_objects_runtime)
    target_sources(test_objects_runtime PRIVATE
        rc-viewer/object_action_visuals.c
    )
endif()

if(TARGET test_streaming_config)
    target_sources(test_streaming_config PRIVATE
        rc-viewer/streaming.c
        rc-viewer/world_transform.c
    )
    target_include_directories(test_streaming_config PRIVATE
        rc-viewer
    )
endif()

if(TARGET test_tick_action_scheduling)
    target_sources(test_tick_action_scheduling PRIVATE
        rc-viewer/tick_pacing.c
    )
    target_include_directories(test_tick_action_scheduling PRIVATE
        rc-viewer
    )
endif()

if(TARGET test_minimap)
    target_sources(test_minimap PRIVATE
        rc-viewer/minimap.c
    )
    target_include_directories(test_minimap PRIVATE
        rc-viewer
        ${CMAKE_SOURCE_DIR}/lib/raylib/include
    )
endif()

if(TARGET test_hitsplats)
    target_sources(test_hitsplats PRIVATE
        rc-viewer/hitsplats.c
        rc-viewer/actor_projection.c
    )
    target_include_directories(test_hitsplats PRIVATE
        rc-viewer
    )
endif()

if(TARGET test_health_bars)
    target_sources(test_health_bars PRIVATE
        rc-viewer/health_bars.c
    )
    target_include_directories(test_health_bars PRIVATE
        rc-viewer
    )
endif()

if(TARGET test_input_feedback)
    target_sources(test_input_feedback PRIVATE
        rc-viewer/input_feedback.c
    )
    target_include_directories(test_input_feedback PRIVATE
        rc-viewer
    )
endif()

if(TARGET test_context_menu)
    target_sources(test_context_menu PRIVATE
        rc-viewer/context_menu.c
    )
    target_include_directories(test_context_menu PRIVATE
        rc-viewer
    )
endif()

if(TARGET test_route_overlay)
    target_sources(test_route_overlay PRIVATE
        rc-viewer/route_overlay.c
    )
    target_include_directories(test_route_overlay PRIVATE
        rc-viewer
    )
endif()

if(TARGET test_streaming_async)
    target_sources(test_streaming_async PRIVATE
        rc-viewer/streaming.c
        rc-viewer/streaming_async.c
    )
    target_include_directories(test_streaming_async PRIVATE
        rc-viewer
    )
    target_link_libraries(test_streaming_async pthread)
endif()

if(TARGET rc-viewer AND TARGET test_streaming_cpu_assets)
    target_sources(test_streaming_cpu_assets PRIVATE
        rc-viewer/streaming.c
    )
    target_include_directories(test_streaming_cpu_assets PRIVATE
        rc-viewer
        ${CMAKE_SOURCE_DIR}/lib/raylib/include
    )
    target_link_directories(test_streaming_cpu_assets PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/raylib/lib
    )
    target_link_libraries(test_streaming_cpu_assets raylib dl pthread m)
    if(UNIX AND NOT APPLE)
        target_link_libraries(test_streaming_cpu_assets OpenGL::GL)
    endif()
endif()

include(${CMAKE_SOURCE_DIR}/tests/tier1_suites.cmake)

if(TARGET rc-viewer)
    set(VIEWER_TEST_SOURCES ${RC_VIEWER_SOURCES})
    list(FILTER VIEWER_TEST_SOURCES EXCLUDE REGEX "/viewer\\.c$")
    add_executable(test_viewer_scene tests/frontend/test_viewer_scene.c ${VIEWER_TEST_SOURCES})
    target_include_directories(test_viewer_scene PRIVATE lib/raylib/include)
    target_link_directories(test_viewer_scene PRIVATE lib/raylib/lib)
    target_link_libraries(test_viewer_scene rc-core rc-content-link raylib m dl pthread)
    if(TARGET OpenGL::GL)
        target_link_libraries(test_viewer_scene OpenGL::GL)
    endif()
    target_compile_definitions(test_viewer_scene PRIVATE RC_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_viewer_scene PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_viewer_scene COMMAND test_viewer_scene)
    set_tests_properties(test_viewer_scene PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(test_dev_spellbooks tests/frontend/test_dev_spellbooks.c
        rc-viewer/dev_validation.c rc-viewer/ui_assets.c rc-viewer/ui_spellbook.c
        rc-viewer/ui_interfaces.c rc-viewer/context_menu.c rc-viewer/prayer_ui.c)
    target_include_directories(test_dev_spellbooks PRIVATE lib/raylib/include)
    target_link_directories(test_dev_spellbooks PRIVATE lib/raylib/lib)
    target_link_libraries(test_dev_spellbooks rc-core rc-content-link raylib m dl pthread)
    if(TARGET OpenGL::GL)
        target_link_libraries(test_dev_spellbooks OpenGL::GL)
    endif()
    target_compile_definitions(test_dev_spellbooks PRIVATE RC_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_dev_spellbooks PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_dev_spellbooks COMMAND test_dev_spellbooks)
    set_tests_properties(test_dev_spellbooks PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_executable(test_prayer_ui tests/frontend/test_prayer_ui.c rc-viewer/prayer_ui.c)
target_link_libraries(test_prayer_ui rc-core m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_prayer_ui PRIVATE -UNDEBUG)
endif()
add_test(NAME test_prayer_ui COMMAND test_prayer_ui)
set_tests_properties(test_prayer_ui PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
