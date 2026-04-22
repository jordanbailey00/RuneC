cmake_minimum_required(VERSION 3.20)
project(RuneC C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

# rc-core: generic game backend library (no render deps, no OSRS-specific
# content). See rc-core/README.md for the engine/content split.
file(GLOB RC_CORE_SOURCES rc-core/*.c)
add_library(rc-core STATIC ${RC_CORE_SOURCES})
target_include_directories(rc-core PUBLIC rc-core)

# rc-content: OSRS-specific content (boss scripts, quests, region code).
# Links against rc-core. Isolation targets select a subset of these .c
# files. See rc-content/README.md.
file(GLOB_RECURSE RC_CONTENT_SOURCES rc-content/*.c)
add_library(rc-content STATIC ${RC_CONTENT_SOURCES})
target_include_directories(rc-content PUBLIC rc-content)
target_link_libraries(rc-content PUBLIC rc-core)

# rc-viewer: raylib frontend (links both libraries).
if(EXISTS "${CMAKE_SOURCE_DIR}/lib/raylib/lib/libraylib.a")
    file(GLOB RC_VIEWER_SOURCES rc-viewer/*.c)
    add_executable(rc-viewer ${RC_VIEWER_SOURCES})
    target_link_libraries(rc-viewer rc-core rc-content)
    target_include_directories(rc-viewer PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/raylib/include
    )
    target_link_directories(rc-viewer PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/raylib/lib
    )
    target_link_libraries(rc-viewer raylib m dl pthread)

    # Platform-specific libs
    if(UNIX AND NOT APPLE)
        find_library(GL_LIB NAMES GL PATHS /usr/lib/x86_64-linux-gnu)
        if(GL_LIB)
            target_link_libraries(rc-viewer ${GL_LIB})
        else()
            target_link_libraries(rc-viewer /usr/lib/x86_64-linux-gnu/libGL.so.1)
        endif()
    endif()
else()
    message(WARNING "Raylib not found at lib/raylib/ — skipping rc-viewer build. "
                    "Place prebuilt raylib in lib/raylib/{include,lib}/")
endif()

# Tests — link both libraries so content-module register fns resolve.
file(GLOB TEST_SOURCES tests/*.c)
foreach(TEST_SRC ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_link_libraries(${TEST_NAME} rc-core rc-content m)
    target_compile_definitions(${TEST_NAME} PRIVATE
        RC_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TEST_NAME} PRIVATE
            "$<$<CONFIG:Release>:-UNDEBUG>"
        )
    elseif(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE
            "$<$<CONFIG:Release>:/UNDEBUG>"
        )
    endif()
    if (NOT TEST_NAME STREQUAL "test_collision")
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
        set_tests_properties(${TEST_NAME} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
    endif()
endforeach()
